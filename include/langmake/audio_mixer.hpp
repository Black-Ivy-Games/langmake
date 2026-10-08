#pragma once
#include "types.hpp"
#include <memory>
#include <cstdint>

namespace lm {
struct MusicScore;
// All allocation occurs during construction or on the command producer thread.
// render() is the sole consumer and never allocates or destroys voices.
class AudioEffects {
public:
    explicit AudioEffects(const Json& effects = Json::array(), int sample_rate = 48000);
    ~AudioEffects();
    void process(float& left, float& right);
    void reset();
    double tail_seconds() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
Json audio_effects_schema();
Json audio_mixer_schema();
Json validate_audio_mixer(const Json& configuration = Json::object());

class GameAudioMixer {
public:
    explicit GameAudioMixer(const Json& configuration = Json::object());
    ~GameAudioMixer();
    void play(float frequency, float seconds = .13f);
    void set_music(std::shared_ptr<const MusicScore> score, bool loop = true, const std::string& bus = "music");
    void play_sfx(std::shared_ptr<const MusicScore> score, const std::string& bus = "sfx");
    void set_music_layer(const std::string& id,float gain,float seconds=.1f,const std::string& quantization="immediate");
    void stop_music();
    void stop_all(bool clear_effects = true);
    void set_bus(const std::string& bus, float gain, float pan = 0, bool mute = false);
    void set_paused(bool paused);
    void render(float* stereo, std::size_t frames);
    Json measurements() const;
    std::uint64_t dropped_commands() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Ordered tick events use the same commands and mixer as the live device. A tick
// is exactly 800 frames at 48 kHz. Events at tick N precede frame N * 800.
// A configure event replaces scene mixer settings and may start its `music` ID.
Json render_game_audio_wav(const std::map<std::string, std::shared_ptr<const MusicScore>>& scores,
    const Json& events, std::uint64_t ticks, const std::filesystem::path& output,
    const Json& configuration = Json::object(), const std::string& initial_music = "");
}
