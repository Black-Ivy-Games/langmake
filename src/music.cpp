#include "langmake/music.hpp"
#include "langmake/audio_mixer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace lm {
namespace {
constexpr double tau = 6.2831853071795864769;
constexpr std::size_t max_notes = 65536;
constexpr int max_voices = 64;
enum class Wave { sine, triangle, pulse, saw, noise };
struct Instrument {
    std::string id;
    Wave wave = Wave::sine;
    double gain = .2, pan = 0, attack = .005, decay = .08, sustain = .6, release = .08;
    double pulse_width = .5, vibrato_hz = 0, vibrato_cents = 0;
};
struct Note {
    int tick = 0, duration = 0, key = 60, slide_to = 60, instrument = 0;
    double velocity = 1;
};
struct Pattern { int length = 0; std::vector<Note> notes; };
struct Reader {
    const AssetSource& source;
    [[noreturn]] void fail(const std::string& path, const std::string& message) const {
        throw std::runtime_error(Json{{"code", "MUSIC_INVALID"}, {"message", message},
            {"file", source.path.generic_string()}, {"path", path}}.dump());
    }
    void object(const Json& value, const std::string& path, std::initializer_list<const char*> keys) const {
        if (!value.is_object()) fail(path, "Expected an object");
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (std::none_of(keys.begin(), keys.end(), [&](const char* key) { return it.key() == key; }))
                fail(path + "/" + it.key(), "Unknown music field");
        }
    }
    double number(const Json& obj, const char* key, double fallback, double minimum, double maximum,
                  const std::string& path, bool required = false) const {
        if (!obj.contains(key)) { if (required) fail(path + "/" + key, "Required field is missing"); return fallback; }
        const auto& value = obj[key];
        if (!value.is_number()) fail(path + "/" + key, "Expected a number");
        double result = value.get<double>();
        if (!std::isfinite(result) || result < minimum || result > maximum)
            fail(path + "/" + key, "Number is outside the supported range");
        return result;
    }
    int integer(const Json& obj, const char* key, int fallback, int minimum, int maximum,
                const std::string& path, bool required = false) const {
        if (!obj.contains(key)) { if (required) fail(path + "/" + key, "Required field is missing"); return fallback; }
        const auto& value = obj[key];
        if (!value.is_number_integer() || value < minimum || value > maximum)
            fail(path + "/" + key, "Expected a bounded integer");
        return value.get<int>();
    }
    std::string string(const Json& obj, const char* key, const std::string& path, bool required = true) const {
        if (!obj.contains(key)) { if (required) fail(path + "/" + key, "Required field is missing"); return {}; }
        if (!obj[key].is_string()) fail(path + "/" + key, "Expected a string");
        auto result = obj[key].get<std::string>();
        if (result.empty() || result.size() > 128) fail(path + "/" + key, "String must contain 1..128 bytes");
        return result;
    }
    void id(const std::string& value, const std::string& path) const {
        if (value.empty() || value.size() > 64 || value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.") != std::string::npos)
            fail(path, "ID must contain 1..64 ASCII letters, digits, _, - or .");
    }
};
Json parse_document(const AssetSource& source, const Reader& reader) {
    if (source.bytes.size() > 1024 * 1024) reader.fail("", "Music source exceeds 1 MiB");
    if (!source.document.is_null()) return source.document;
    std::vector<std::set<std::string>> keys;
    return Json::parse(source.bytes, [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 20) reader.fail("", "Music JSON nesting exceeds 20 levels");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
            reader.fail("", "Duplicate music object key: " + value.get<std::string>());
        if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
    });
}
double gate_envelope(const Instrument& instrument, double time) {
    if (instrument.attack > 0 && time < instrument.attack) return time / instrument.attack;
    time -= instrument.attack;
    if (instrument.decay > 0 && time < instrument.decay)
        return 1 + (instrument.sustain - 1) * time / instrument.decay;
    return instrument.sustain;
}
double poly_blep(double phase, double increment) {
    if (phase < increment) { double t = phase / increment; return t + t - t*t - 1; }
    if (phase > 1 - increment) { double t = (phase - 1) / increment; return t*t + t + t + 1; }
    return 0;
}
}

struct MusicScore {
    std::string title, revision;
    double bpm = 120, gain = .7, duration = 0;
    int ppq = 96, length = 0, polyphony = 32;
    uint32_t seed = 1;
    Json effects = Json::array();
    double tail = 0;
    std::vector<Instrument> instruments;
    std::vector<Note> notes;
    double seconds_per_tick() const { return 60.0 / (bpm * ppq); }
    uint64_t render_frames(int rate) const {
        uint64_t result = uint64_t(std::ceil(length * seconds_per_tick() * rate));
        for (const auto& note : notes) {
            const auto end = uint64_t(std::llround(static_cast<long double>(note.tick + note.duration) * 60.0L * rate / (bpm * ppq)));
            result = std::max(result, end + uint64_t(std::ceil(instruments[note.instrument].release * rate)));
        }
        return result + uint64_t(std::ceil(tail * rate));
    }
};

std::shared_ptr<const MusicScore> compile_music(const AssetSource& source) {
    Reader reader{source};
    try {
        if (source.type != "music") reader.fail("/type", "Asset must have type music");
        const auto doc = parse_document(source, reader);
        reader.object(doc, "", {"schema_version", "type", "title", "bpm", "ppq", "length", "gain", "seed", "polyphony", "instruments", "notes", "patterns", "arrangement", "effects", "tail_seconds"});
        reader.integer(doc, "schema_version", 0, 1, 1, "", true);
        if (reader.string(doc, "type", "") != "music") reader.fail("/type", "Expected type music, including for short SFX");
        auto score = std::make_shared<MusicScore>();
        score->title = reader.string(doc, "title", "", false);
        score->revision = source.revision;
        score->bpm = reader.number(doc, "bpm", 120, 20, 400, "");
        score->ppq = reader.integer(doc, "ppq", 96, 1, 960, "");
        score->length = reader.integer(doc, "length", 0, 1, 1000000, "", true);
        score->polyphony = reader.integer(doc, "polyphony", 32, 1, max_voices, "");
        score->gain = reader.number(doc, "gain", .7, 0, 1, "");
        score->seed = uint32_t(reader.integer(doc, "seed", 1, 1, 2147483647, ""));
        if (doc.contains("effects")) score->effects = doc["effects"];
        double default_tail = 0;
        try {
            AudioEffects effects(score->effects);
            default_tail = effects.tail_seconds();
        } catch (const std::exception& error) { reader.fail("/effects", error.what()); }
        score->tail = reader.number(doc, "tail_seconds", default_tail, 0, 10, "");
        if (!doc.contains("instruments") || !doc["instruments"].is_object() || doc["instruments"].empty() || doc["instruments"].size() > 64)
            reader.fail("/instruments", "Expected 1..64 named instruments");
        std::map<std::string, int> instruments;
        for (auto it = doc["instruments"].begin(); it != doc["instruments"].end(); ++it) {
            const std::string path = "/instruments/" + it.key();
            reader.id(it.key(), path);
            reader.object(it.value(), path, {"wave", "gain", "pan", "attack", "decay", "sustain", "release", "pulse_width", "vibrato_hz", "vibrato_cents"});
            Instrument instrument;
            instrument.id = it.key();
            const auto wave = reader.string(it.value(), "wave", path);
            const std::map<std::string, Wave> waves{{"sine", Wave::sine}, {"triangle", Wave::triangle}, {"pulse", Wave::pulse}, {"saw", Wave::saw}, {"noise", Wave::noise}};
            if (!waves.contains(wave)) reader.fail(path + "/wave", "Unknown waveform; use sine, triangle, pulse, saw or noise");
            instrument.wave = waves.at(wave);
            instrument.gain = reader.number(it.value(), "gain", .2, 0, 1, path);
            instrument.pan = reader.number(it.value(), "pan", 0, -1, 1, path);
            instrument.attack = reader.number(it.value(), "attack", .005, 0, 5, path);
            instrument.decay = reader.number(it.value(), "decay", .08, 0, 5, path);
            instrument.sustain = reader.number(it.value(), "sustain", .6, 0, 1, path);
            instrument.release = reader.number(it.value(), "release", .08, 0, 5, path);
            instrument.pulse_width = reader.number(it.value(), "pulse_width", .5, .05, .95, path);
            instrument.vibrato_hz = reader.number(it.value(), "vibrato_hz", 0, 0, 20, path);
            instrument.vibrato_cents = reader.number(it.value(), "vibrato_cents", 0, 0, 1200, path);
            instruments.emplace(it.key(), int(score->instruments.size()));
            score->instruments.push_back(instrument);
        }
        std::size_t authored_notes = 0;
        auto parse_notes = [&](const Json& list, int length, const std::string& path) {
            if (!list.is_array() || list.size() > max_notes) reader.fail(path, "Expected at most 65536 notes");
            authored_notes += list.size();
            if (authored_notes > max_notes) reader.fail(path, "Total authored notes exceed 65536");
            std::vector<Note> result;
            result.reserve(list.size());
            for (std::size_t i = 0; i < list.size(); ++i) {
                const auto& item = list[i]; const auto item_path = path + "/" + std::to_string(i);
                reader.object(item, item_path, {"tick", "duration", "note", "instrument", "velocity", "slide_to"});
                Note note;
                note.tick = reader.integer(item, "tick", 0, 0, length - 1, item_path, true);
                note.duration = reader.integer(item, "duration", 0, 1, length, item_path, true);
                if (note.duration > length - note.tick) reader.fail(item_path + "/duration", "Note extends past its score or pattern length");
                note.key = reader.integer(item, "note", 0, 0, 127, item_path, true);
                note.slide_to = reader.integer(item, "slide_to", note.key, 0, 127, item_path);
                note.velocity = reader.number(item, "velocity", 1, 0, 1, item_path);
                auto instrument = reader.string(item, "instrument", item_path);
                if (!instruments.contains(instrument)) reader.fail(item_path + "/instrument", "Unknown instrument ID");
                note.instrument = instruments.at(instrument);
                result.push_back(note);
            }
            return result;
        };
        if (doc.contains("notes")) score->notes = parse_notes(doc["notes"], score->length, "/notes");
        std::map<std::string, Pattern> patterns;
        if (doc.contains("patterns")) {
            if (!doc["patterns"].is_object() || doc["patterns"].size() > 256) reader.fail("/patterns", "Expected at most 256 named patterns");
            for (auto it = doc["patterns"].begin(); it != doc["patterns"].end(); ++it) {
                const std::string path = "/patterns/" + it.key();
                reader.id(it.key(), path);
                reader.object(it.value(), path, {"length", "notes"});
                Pattern pattern;
                pattern.length = reader.integer(it.value(), "length", 0, 1, 1000000, path, true);
                if (!it.value().contains("notes")) reader.fail(path + "/notes", "Pattern notes are required");
                pattern.notes = parse_notes(it.value()["notes"], pattern.length, path + "/notes");
                patterns.emplace(it.key(), std::move(pattern));
            }
        }
        if (doc.contains("arrangement")) {
            const auto& arrangement = doc["arrangement"];
            if (!arrangement.is_array() || arrangement.size() > 1024) reader.fail("/arrangement", "Expected at most 1024 arrangement entries");
            for (std::size_t i = 0; i < arrangement.size(); ++i) {
                const auto& item = arrangement[i]; const auto path = "/arrangement/" + std::to_string(i);
                reader.object(item, path, {"pattern", "tick", "repeat", "transpose"});
                const auto name = reader.string(item, "pattern", path);
                if (!patterns.contains(name)) reader.fail(path + "/pattern", "Unknown pattern ID");
                const auto& pattern = patterns.at(name);
                const int tick = reader.integer(item, "tick", 0, 0, score->length - 1, path, true);
                const int repeat = reader.integer(item, "repeat", 1, 1, 128, path);
                const int transpose = reader.integer(item, "transpose", 0, -127, 127, path);
                if (int64_t(tick) + int64_t(pattern.length) * repeat > score->length)
                    reader.fail(path, "Pattern repetitions extend past score length");
                if (score->notes.size() + pattern.notes.size() * std::size_t(repeat) > max_notes)
                    reader.fail(path, "Expanded note count exceeds 65536");
                for (int iteration = 0; iteration < repeat; ++iteration) for (auto note : pattern.notes) {
                    note.tick += tick + iteration * pattern.length;
                    note.key += transpose; note.slide_to += transpose;
                    if (note.key < 0 || note.key > 127 || note.slide_to < 0 || note.slide_to > 127)
                        reader.fail(path + "/transpose", "Transposed pitch is outside MIDI 0..127");
                    score->notes.push_back(note);
                }
            }
        }
        std::stable_sort(score->notes.begin(), score->notes.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
        score->duration = score->length * score->seconds_per_tick();
        for (const auto& note : score->notes)
            score->duration = std::max(score->duration, (note.tick + note.duration) * score->seconds_per_tick() + score->instruments[note.instrument].release);
        score->duration += score->tail;
        if (score->duration > 600) reader.fail("/length", "Score including release and effect tails may not exceed 600 seconds");
        return score;
    } catch (const Json::exception& error) {
        reader.fail("", std::string("Invalid music JSON: ") + error.what());
    }
}

Json inspect_music(const MusicScore& score) {
    return {{"type", "music"}, {"title", score.title}, {"source_revision", score.revision},
        {"bpm", score.bpm}, {"ppq", score.ppq}, {"length", score.length}, {"notes", score.notes.size()},
        {"instruments", score.instruments.size()}, {"polyphony", score.polyphony}, {"voice_policy", "oldest"},
        {"loop_seconds", score.length * score.seconds_per_tick()}, {"duration_seconds", score.duration},
        {"sample_rate", 48000}, {"channels", 2}, {"sample_frames", score.render_frames(48000)},
        {"effects", score.effects}, {"effect_tail_seconds", score.tail}};
}

Json music_schema() {
    auto number = [](double lo, double hi, double fallback) { return Json{{"type", "number"}, {"minimum", lo}, {"maximum", hi}, {"default", fallback}}; };
    auto integer = [](int lo, int hi) { return Json{{"type", "integer"}, {"minimum", lo}, {"maximum", hi}}; };
    Json note{{"type", "object"}, {"additionalProperties", false}, {"required", {"tick", "duration", "note", "instrument"}},
        {"properties", {{"tick", integer(0, 999999)}, {"duration", integer(1, 1000000)}, {"note", integer(0, 127)},
            {"instrument", {{"type", "string"}, {"minLength", 1}, {"maxLength", 64}}},
            {"velocity", number(0, 1, 1)}, {"slide_to", integer(0, 127)}}}};
    Json notes{{"type", "array"}, {"maxItems", max_notes}, {"items", note}};
    Json instrument{{"type", "object"}, {"additionalProperties", false}, {"required", {"wave"}},
        {"properties", {{"wave", {{"enum", {"sine", "triangle", "pulse", "saw", "noise"}}}}, {"gain", number(0, 1, .2)},
            {"pan", number(-1, 1, 0)}, {"attack", number(0, 5, .005)}, {"decay", number(0, 5, .08)},
            {"sustain", number(0, 1, .6)}, {"release", number(0, 5, .08)}, {"pulse_width", number(.05, .95, .5)},
            {"vibrato_hz", number(0, 20, 0)}, {"vibrato_cents", number(0, 1200, 0)}}}};
    Json pattern{{"type", "object"}, {"additionalProperties", false}, {"required", {"length", "notes"}},
        {"properties", {{"length", integer(1, 1000000)}, {"notes", notes}}}};
    Json arrangement{{"type", "object"}, {"additionalProperties", false}, {"required", {"pattern", "tick"}},
        {"properties", {{"pattern", {{"type", "string"}, {"minLength", 1}, {"maxLength", 64}}},
            {"tick", integer(0, 999999)}, {"repeat", integer(1, 128)}, {"transpose", integer(-127, 127)}}}};
    Json id{{"type", "string"}, {"pattern", "^[A-Za-z0-9_.-]{1,64}$"}};
    return {{"$schema", "https://json-schema.org/draft/2020-12/schema"}, {"title", "Langmake native music and SFX v1"},
        {"type", "object"}, {"additionalProperties", false}, {"required", {"schema_version", "type", "length", "instruments"}},
        {"properties", {{"schema_version", {{"const", 1}}}, {"type", {{"const", "music"}}},
            {"title", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}, {"bpm", number(20, 400, 120)},
            {"ppq", integer(1, 960)}, {"length", integer(1, 1000000)}, {"gain", number(0, 1, .7)},
            {"seed", integer(1, 2147483647)}, {"polyphony", integer(1, max_voices)},
            {"effects", audio_effects_schema()}, {"tail_seconds", number(0,10,0)},
            {"instruments", {{"type", "object"}, {"minProperties", 1}, {"maxProperties", 64}, {"propertyNames", id}, {"additionalProperties", instrument}}},
            {"notes", notes}, {"patterns", {{"type", "object"}, {"maxProperties", 256}, {"propertyNames", id}, {"additionalProperties", pattern}}},
            {"arrangement", {{"type", "array"}, {"maxItems", 1024}, {"items", arrangement}}}}},
        {"$comment", "Ticks use ppq divisions per quarter note. Envelopes use seconds. Compile checks IDs, note/pattern endpoints, transposed pitches, 65536 total authored/expanded notes, and a 600 second maximum including release tails. Rests are gaps. Concurrent patterns form tracks; oldest voices are stolen at the polyphony limit."}};
}

struct MusicVoice::Impl {
    struct Voice {
        bool active = false;
        std::size_t note = 0;
        uint64_t start = 0, gate = 0, release = 0, order = 0;
        double phase = 0, left = 0, right = 0, increment = 0;
        uint32_t noise = 1;
    };
    std::shared_ptr<const MusicScore> score;
    bool loop;
    int sample_rate = 0;
    uint64_t frame = 0, cycle = 0, next_frame = 0, serial = 0, finish_frame = 0;
    std::size_t next_note = 0;
    bool exhausted = false;
    std::array<Voice, max_voices> voices{};
    std::unique_ptr<AudioEffects> effects;
    int effects_rate = 0;
    explicit Impl(std::shared_ptr<const MusicScore> value, bool repeat) : score(std::move(value)), loop(repeat) {
        if (!score) throw std::runtime_error("MusicVoice requires a compiled score");
    }
    uint64_t tick_frame(uint64_t tick) const {
        return uint64_t(std::llround(static_cast<long double>(tick) * 60.0L * sample_rate / (score->bpm * score->ppq)));
    }
    void schedule() {
        if (score->notes.empty()) { exhausted = true; return; }
        if (next_note == score->notes.size()) {
            if (!loop) { exhausted = true; return; }
            next_note = 0; ++cycle;
        }
        next_frame = tick_frame(cycle * uint64_t(score->length) + score->notes[next_note].tick);
    }
    void trigger() {
        Voice* selected = nullptr;
        for (int i = 0; i < score->polyphony; ++i) if (!voices[i].active) { selected = &voices[i]; break; }
        if (!selected) selected = &*std::min_element(voices.begin(), voices.begin() + score->polyphony,
            [](const Voice& a, const Voice& b) { return a.order < b.order; });
        const auto& note = score->notes[next_note];
        const auto& instrument = score->instruments[note.instrument];
        *selected = Voice{};
        selected->active = true; selected->note = next_note; selected->start = frame; selected->order = ++serial;
        selected->gate = tick_frame(cycle * uint64_t(score->length) + note.tick + note.duration) - next_frame;
        selected->release = uint64_t(std::ceil(instrument.release * sample_rate));
        selected->increment = std::min(.45, 440 * std::pow(2.0, (note.key - 69) / 12.0) / sample_rate);
        selected->noise = score->seed ^ (uint32_t(next_note + 1) * 0x9e3779b9U) ^ (uint32_t(cycle) * 0x85ebca6bU);
        if (selected->noise == 0) selected->noise = 1;
        const double amplitude = score->gain * instrument.gain * note.velocity;
        selected->left = std::cos((instrument.pan + 1) * tau / 8) * amplitude;
        selected->right = std::sin((instrument.pan + 1) * tau / 8) * amplitude;
        ++next_note; schedule();
    }
};

MusicVoice::MusicVoice(std::shared_ptr<const MusicScore> score, bool loop) : impl_(std::make_unique<Impl>(std::move(score), loop)) {}
MusicVoice::~MusicVoice() = default;
void MusicVoice::reset() {
    impl_->sample_rate = 0; impl_->frame = 0; impl_->cycle = 0; impl_->next_note = 0; impl_->next_frame = 0;
    impl_->serial = 0; impl_->exhausted = false; impl_->finish_frame = 0;
    for (auto& voice : impl_->voices) voice = Impl::Voice{};
    if (impl_->effects) impl_->effects->reset();
}
bool MusicVoice::finished() const { return !impl_->loop && impl_->sample_rate != 0 && impl_->frame >= impl_->finish_frame; }
void MusicVoice::render(float* stereo, std::size_t frames, int sample_rate) {
    auto& state = *impl_;
    if ((!stereo && frames) || sample_rate < 8000 || sample_rate > 192000) throw std::runtime_error("Invalid music render buffer or sample rate");
    if (state.sample_rate && state.sample_rate != sample_rate) throw std::runtime_error("Reset MusicVoice before changing its sample rate");
    if (!state.sample_rate) {
        if (!state.score->effects.empty() && state.effects_rate != sample_rate) {
            state.effects = std::make_unique<AudioEffects>(state.score->effects, sample_rate);
            state.effects_rate = sample_rate;
        }
        state.sample_rate = sample_rate;
        state.finish_frame = state.score->render_frames(sample_rate);
        state.schedule();
    }
    for (std::size_t i = 0; i < frames; ++i, ++state.frame) {
        while (!state.exhausted && state.frame >= state.next_frame) state.trigger();
        double left = 0, right = 0;
        for (int v = 0; v < state.score->polyphony; ++v) {
            auto& voice = state.voices[v];
            if (!voice.active) continue;
            const uint64_t age = state.frame - voice.start;
            if (age >= voice.gate + voice.release) { voice.active = false; continue; }
            const auto& note = state.score->notes[voice.note];
            const auto& instrument = state.score->instruments[note.instrument];
            const double seconds = double(age) / sample_rate;
            double envelope;
            if (age < voice.gate) envelope = gate_envelope(instrument, seconds);
            else envelope = gate_envelope(instrument, double(voice.gate) / sample_rate) *
                (1 - double(age - voice.gate) / std::max<uint64_t>(1, voice.release));
            double increment = voice.increment;
            if (note.slide_to != note.key || (instrument.vibrato_hz > 0 && instrument.vibrato_cents > 0)) {
                const double key = note.key + (note.slide_to - note.key) * std::min(1.0, double(age) / std::max<uint64_t>(1, voice.gate));
                const double vibrato = std::sin(tau * instrument.vibrato_hz * seconds) * instrument.vibrato_cents / 100;
                increment = std::min(.45, 440 * std::pow(2.0, (key - 69 + vibrato) / 12) / sample_rate);
            }
            double sample = 0;
            switch (instrument.wave) {
            case Wave::sine: sample = std::sin(tau * voice.phase); break;
            case Wave::triangle: sample = 1 - 4 * std::abs(voice.phase - .5); break;
            case Wave::saw: sample = 2 * voice.phase - 1 - poly_blep(voice.phase, increment); break;
            case Wave::pulse: {
                sample = voice.phase < instrument.pulse_width ? 1 : -1;
                sample += poly_blep(voice.phase, increment);
                double other = voice.phase - instrument.pulse_width;
                if (other < 0) other += 1;
                sample -= poly_blep(other, increment);
                break;
            }
            case Wave::noise:
                voice.noise ^= voice.noise << 13; voice.noise ^= voice.noise >> 17; voice.noise ^= voice.noise << 5;
                sample = double(voice.noise) / 2147483648.0 - 1;
                break;
            }
            sample *= envelope;
            left += sample * voice.left; right += sample * voice.right;
            voice.phase += increment; voice.phase -= std::floor(voice.phase);
        }
        stereo[2*i] = float(left); stereo[2*i+1] = float(right);
        if (state.effects && (state.loop || state.frame < state.finish_frame)) state.effects->process(stereo[2*i], stereo[2*i+1]);
    }
}

Json render_music_wav(std::shared_ptr<const MusicScore> score, const std::filesystem::path& output, double seconds) {
    if (!score || !std::isfinite(seconds) || seconds < 0 || seconds > 600)
        throw std::runtime_error("WAV rendering requires a score and seconds in 0..600");
    constexpr uint32_t sample_rate = 48000;
    const uint64_t frames = seconds > 0 ? uint64_t(std::ceil(seconds * sample_rate)) : score->render_frames(sample_rate);
    if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot write WAV: " + output.string());
    auto u16 = [&](uint16_t value) { out.put(char(value & 255)); out.put(char(value >> 8)); };
    auto u32 = [&](uint32_t value) { u16(uint16_t(value)); u16(uint16_t(value >> 16)); };
    const uint32_t data_bytes = uint32_t(frames * 4);
    out.write("RIFF", 4); u32(36 + data_bytes); out.write("WAVEfmt ", 8); u32(16); u16(1); u16(2);
    u32(sample_rate); u32(sample_rate * 4); u16(4); u16(16); out.write("data", 4); u32(data_bytes);
    MusicVoice voice(score, false);
    std::array<float, 1024> buffer{};
    double peak = 0, energy = 0;
    uint64_t clipped = 0;
    for (uint64_t position = 0; position < frames;) {
        const auto count = std::size_t(std::min<uint64_t>(frames - position, buffer.size() / 2));
        voice.render(buffer.data(), count, sample_rate);
        for (std::size_t i = 0; i < count; ++i) {
            bool clipping = false;
            for (int channel = 0; channel < 2; ++channel) {
                double sample = buffer[2*i+channel];
                peak = std::max(peak, std::abs(sample)); energy += sample * sample;
                clipping |= std::abs(sample) > 1;
                auto pcm = int16_t(std::lrint(std::clamp(sample, -1.0, 1.0) * 32767));
                u16(uint16_t(pcm));
            }
            if (clipping) ++clipped;
        }
        position += count;
    }
    out.close();
    if (!out) throw std::runtime_error("WAV write was incomplete: " + output.string());
    return {{"ok", true}, {"output", std::filesystem::absolute(output).string()}, {"sample_rate", sample_rate},
        {"channels", 2}, {"bits_per_sample", 16}, {"sample_frames", frames}, {"duration_seconds", double(frames) / sample_rate},
        {"peak", peak}, {"rms", frames ? std::sqrt(energy / (frames * 2)) : 0}, {"clipped_frames", clipped},
        {"source_revision", score->revision}};
}
}
