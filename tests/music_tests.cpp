#include "langmake/music.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace { std::atomic<bool> forbid_allocations{false}; }
#if defined(__GNUC__)
#define LM_TEST_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define LM_TEST_NOINLINE __declspec(noinline)
#else
#define LM_TEST_NOINLINE
#endif
LM_TEST_NOINLINE void* operator new(std::size_t size) {
    if (forbid_allocations.load()) throw std::bad_alloc();
    if (auto* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
LM_TEST_NOINLINE void* operator new[](std::size_t size) { return ::operator new(size); }
LM_TEST_NOINLINE void operator delete(void* pointer) noexcept { std::free(pointer); }
LM_TEST_NOINLINE void operator delete[](void* pointer) noexcept { std::free(pointer); }
LM_TEST_NOINLINE void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
LM_TEST_NOINLINE void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
#undef LM_TEST_NOINLINE

namespace {
using lm::Json;
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
lm::AssetSource source(Json document) {
    lm::AssetSource result;
    result.type = "music"; result.path = "assets/test.music.json"; result.revision = "music-test";
    result.document = std::move(document);
    return result;
}
Json basic() {
    return {{"schema_version", 1}, {"type", "music"}, {"bpm", 120}, {"ppq", 96}, {"length", 96},
        {"instruments", {{"lead", {{"wave", "sine"}, {"gain", .2}, {"attack", .01}, {"release", .1}}}}},
        {"notes", Json::array({{{"tick", 24}, {"duration", 24}, {"note", 69}, {"instrument", "lead"}}})}};
}
std::vector<float> render(std::shared_ptr<const lm::MusicScore> score, std::size_t frames, std::size_t block, bool loop = false) {
    lm::MusicVoice voice(score, loop);
    std::vector<float> result(frames * 2);
    for (std::size_t offset = 0; offset < frames; offset += block)
        voice.render(result.data() + offset * 2, std::min(block, frames - offset));
    return result;
}
void rejects(Json document) {
    try { lm::compile_music(source(std::move(document))); }
    catch (const std::runtime_error& error) {
        auto diagnostic = Json::parse(error.what());
        require(diagnostic["code"] == "MUSIC_INVALID" && diagnostic.contains("file") && diagnostic.contains("path"),
                "Invalid music must include structured source context");
        return;
    }
    throw std::runtime_error("Malformed music was accepted");
}
uint32_t u32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    return uint32_t(bytes.at(offset)) | (uint32_t(bytes.at(offset + 1)) << 8) |
        (uint32_t(bytes.at(offset + 2)) << 16) | (uint32_t(bytes.at(offset + 3)) << 24);
}
void envelope_and_timing() {
    auto score = lm::compile_music(source(basic()));
    auto inspected = lm::inspect_music(*score);
    require(inspected["sample_frames"] == 24000 && inspected["duration_seconds"] == .5,
            "96 ticks at 120 BPM and PPQ96 must last half a second");
    auto samples = render(score, 48000, 257);
    require(std::all_of(samples.begin(), samples.begin() + 12000, [](float x) { return x == 0; }), "Initial rest must remain exactly silent");
    require(std::any_of(samples.begin() + 12000, samples.begin() + 33600, [](float x) { return std::abs(x) > .01f; }), "Note must sound during its envelope");
    require(std::all_of(samples.begin() + 33600, samples.end(), [](float x) { return x == 0; }), "Release must end and padded one-shot output stay silent");
    require(std::all_of(samples.begin(), samples.end(), [](float x) { return std::isfinite(x) && std::abs(x) <= .2f; }), "Envelope and gain must bound the signal");
    auto panned = basic(); panned["instruments"]["lead"]["pan"] = -1;
    auto left = render(lm::compile_music(source(panned)), 24000, 1024);
    for (std::size_t i = 0; i < left.size() / 2; ++i) require(left[2*i+1] == 0, "Hard-left pan must silence the right channel");
    lm::MusicVoice voice(score, false);
    std::array<float, 48000> exact{};
    voice.render(exact.data(), 24000);
    require(voice.finished(), "One-shot must finish at its exact sample count");
    voice.reset(); require(!voice.finished(), "Reset must reset completion state");
    voice.render(exact.data(), 24000);
    require(std::equal(exact.begin(), exact.end(), samples.begin()), "Reset must reproduce the complete waveform");
}
void deterministic_blocks_and_patterns() {
    auto document = basic();
    document["instruments"]["lead"]["wave"] = "noise";
    document["seed"] = 1947;
    document["notes"][0]["slide_to"] = 45;
    auto score = lm::compile_music(source(document));
    auto whole = render(score, 96000, 96000, true);
    require(whole == render(score, 96000, 127, true) && whole == render(score, 96000, 1, true),
            "Noise and looping must be sample-identical for different callback block sizes");
    document["seed"] = 1948;
    require(whole != render(lm::compile_music(source(document)), 96000, 257, true), "Noise seed must affect output");
    auto pattern = basic(); pattern["notes"] = Json::array();
    pattern["patterns"] = {{"riff", {{"length", 24}, {"notes", Json::array({
        {{"tick", 0}, {"duration", 8}, {"note", 48}, {"instrument", "lead"}},
        {{"tick", 12}, {"duration", 8}, {"note", 55}, {"instrument", "lead"}}})}}}};
    pattern["arrangement"] = Json::array({{{"tick", 12}, {"pattern", "riff"}, {"repeat", 3}, {"transpose", 12}}});
    auto direct = basic(); direct["notes"] = Json::array();
    for (int tick : {12, 36, 60}) {
        direct["notes"].push_back({{"tick", tick}, {"duration", 8}, {"note", 60}, {"instrument", "lead"}});
        direct["notes"].push_back({{"tick", tick + 12}, {"duration", 8}, {"note", 67}, {"instrument", "lead"}});
    }
    auto arranged = lm::compile_music(source(pattern));
    require(lm::inspect_music(*arranged)["notes"] == 6, "Arrangement must expand repeat count");
    require(render(arranged, 30000, 239) == render(lm::compile_music(source(direct)), 30000, 512),
            "Pattern repeats and transposition must equal authored direct notes");
    std::vector<float> buffer(2048);
    lm::MusicVoice allocation_test(score, true);
    struct Guard { Guard() { forbid_allocations = true; } ~Guard() { forbid_allocations = false; } };
    { Guard guard; allocation_test.render(buffer.data(), 1024); allocation_test.reset(); allocation_test.render(buffer.data(), 1024); }
}
void fractional_loop_clock() {
    auto document = basic();
    document["bpm"] = 137; document["length"] = 7;
    document["instruments"]["lead"] = {{"wave", "noise"}, {"attack", 0}, {"decay", 0}, {"sustain", 1}, {"release", 0}};
    document["notes"] = Json::array({{{"tick", 0}, {"duration", 1}, {"note", 60}, {"instrument", "lead"}}});
    const auto samples = render(lm::compile_music(source(document)), 7000, 251, true);
    std::vector<int> starts;
    bool sounded = false;
    for (int i = 0; i < 7000; ++i) {
        bool sound = samples[2*i] != 0;
        if (sound && !sounded) starts.push_back(i);
        sounded = sound;
    }
    require(starts == std::vector<int>({0, 1533, 3066, 4599, 6131}), "Non-integral tempo must accumulate fractional samples across loops");
}
void malformed() {
    auto value = basic(); value["typo"] = 1; rejects(value);
    value = basic(); value["ppq"] = 96.5; rejects(value);
    value = basic(); value["seed"] = 0; rejects(value);
    value = basic(); value["polyphony"] = 65; rejects(value);
    value = basic(); value["notes"][0]["instrument"] = "missing"; rejects(value);
    value = basic(); value["notes"][0]["duration"] = 97; rejects(value);
    value = basic(); value["notes"][0]["tick"] = 18446744073709551615ULL; rejects(value);
    value = basic(); value["instruments"]["lead"]["attack"] = -1; rejects(value);
    value = basic(); value["instruments"]["lead"]["wave"] = "guitar"; rejects(value);
    value = basic(); value["instruments"]["lead"]["vibrato_cents"] = std::numeric_limits<double>::infinity(); rejects(value);
    value = basic(); value["arrangement"] = Json::array({{{"pattern", "missing"}, {"tick", 0}}}); rejects(value);
    value = basic(); value["length"] = 1000000; rejects(value);
    value = basic(); value["patterns"] = {{"a", {{"length", 96}, {"notes", value["notes"]}}}};
    value["arrangement"] = Json::array({{{"pattern", "a"}, {"tick", 1}}}); rejects(value);
    value["arrangement"][0]["tick"] = 0; value["arrangement"][0]["transpose"] = 100; rejects(value);
    value = basic(); value["length"] = 128;
    Json repeated = Json::array();
    for (int i = 0; i < 513; ++i) repeated.push_back({{"tick", 0}, {"duration", 1}, {"note", 60}, {"instrument", "lead"}});
    value["patterns"] = {{"many", {{"length", 1}, {"notes", repeated}}}};
    value["arrangement"] = Json::array({{{"pattern", "many"}, {"tick", 0}, {"repeat", 128}}}); rejects(value);
    auto duplicate = source(nullptr); duplicate.bytes = "{\"schema_version\":1,\"schema_version\":1}";
    try { lm::compile_music(duplicate); throw std::logic_error("Duplicate music keys accepted"); }
    catch (const std::runtime_error& error) { require(Json::parse(error.what())["code"] == "MUSIC_INVALID", "Duplicate key diagnostic missing"); }
}
void wav_and_polyphony(const std::filesystem::path& root) {
    auto folder = root / ".cache/music-tests" / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto file = folder / "note.wav";
    auto score = lm::compile_music(source(basic()));
    auto report = lm::render_music_wav(score, file);
    std::ifstream in(file, std::ios::binary);
    std::vector<unsigned char> bytes(std::istreambuf_iterator<char>(in), {});
    require(bytes.size() == 44 + 24000 * 4 && u32(bytes, 4) == bytes.size() - 8 && u32(bytes, 40) == 24000 * 4,
            "RIFF and data chunk sizes must match exact stereo PCM frame count");
    require(std::string(bytes.begin(), bytes.begin() + 4) == "RIFF" && std::string(bytes.begin() + 8, bytes.begin() + 12) == "WAVE",
            "Offline output must have a RIFF/WAVE header");
    require(u32(bytes, 24) == 48000 && bytes[22] == 2 && bytes[34] == 16, "WAV must be 48kHz stereo signed16 PCM");
    require(report["sample_frames"] == 24000 && report["peak"].get<double>() > .01 && report["clipped_frames"] == 0, "Render report must describe audible unclipped PCM");
    require(lm::render_music_wav(score, folder / "trim.wav", .1)["sample_frames"] == 4800, "Explicit duration must trim to exact frames");
    auto loud = basic(); loud["gain"] = 1;
    loud["instruments"]["lead"] = {{"wave", "triangle"}, {"gain", 1}, {"attack", 0}, {"decay", 0}, {"sustain", 1}, {"release", 0}};
    loud["notes"] = Json::array({{{"tick", 0}, {"duration", 96}, {"note", 69}, {"instrument", "lead"}},
                                 {{"tick", 0}, {"duration", 96}, {"note", 69}, {"instrument", "lead"}}});
    auto clipped = lm::render_music_wav(lm::compile_music(source(loud)), folder / "clip.wav");
    require(clipped["peak"].get<double>() > 1 && clipped["clipped_frames"].get<int>() > 0, "Clipping must be measured before PCM saturation");
    loud["polyphony"] = 1;
    auto limited = lm::render_music_wav(lm::compile_music(source(loud)), folder / "limited.wav");
    require(limited["clipped_frames"] == 0, "Polyphony limit must bound simultaneous voices");
}
}
int main(int argc, char** argv) {
    try {
        envelope_and_timing(); deterministic_blocks_and_patterns(); fractional_loop_clock(); malformed();
        wav_and_polyphony(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path());
        require(lm::music_schema()["additionalProperties"] == false, "Music schema must forbid unknown fields");
        std::cout << "PASS music timing, envelopes, patterns, deterministic blocks/noise, allocation-free rendering, strict validation and WAV evidence\n";
        return 0;
    } catch (const std::exception& error) { forbid_allocations = false; std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
