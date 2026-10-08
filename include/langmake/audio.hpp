#pragma once
#include "types.hpp"
#include <memory>
#include <cstdint>
namespace lm {
struct MusicScore;
class Audio {
public:
    explicit Audio(bool enabled, const Json& configuration = Json::object());
    ~Audio();
    void play(float frequency, float seconds = 0.13f);
    // Submit commands from the owning game thread. Assets are compiled before
    // submission; audio rendering never loads assets or executes game scripts.
    void set_music(std::shared_ptr<const MusicScore> score, bool loop = true, const std::string& bus = "music");
    void set_music_layer(const std::string& id,float gain,float seconds=.1f,const std::string& quantization="immediate");
    void stop_music();
    void stop_all();
    void play_sfx(std::shared_ptr<const MusicScore> score, const std::string& bus = "sfx");
    void set_bus(const std::string& bus, float gain, float pan = 0, bool mute = false);
    // Rebuild source-authored effects/buses on the game thread while the device
    // is stopped. Invalid configuration leaves the previous mixer intact.
    void configure(const Json& configuration);
    void set_paused(bool paused);
    // Stop/start the physical device for platform lifecycle changes while
    // retaining voices, mixer state, and the logical pause setting.
    bool set_suspended(bool suspended);
    // Process backend interruption notifications on the owning game thread.
    // Device callbacks only publish atomic flags; they never restart devices.
    bool pump_device();
    // A started null backend can advance the mixer but supplies no real output.
    // This reports a usable output backend, not human confirmation of audibility.
    bool available() const;
    std::uint64_t dropped_commands() const;
    Json measurements() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
