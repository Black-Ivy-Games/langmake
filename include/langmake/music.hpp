#pragma once
#include "types.hpp"
#include <memory>
#include <cstdint>

namespace lm {
struct MusicScore;
std::shared_ptr<const MusicScore> compile_music(const AssetSource& source);
Json music_schema();
Json inspect_music(const MusicScore& score);
class MusicVoice {
public:
    explicit MusicVoice(std::shared_ptr<const MusicScore> score, bool loop = true);
    ~MusicVoice();
    // Prepare with render(nullptr,0,rate) before entering a realtime callback;
    // effect delay buffers are allocated once per sample rate, then reused.
    void render(float* stereo, std::size_t frames, int sample_rate = 48000);
    void reset();
    bool finished() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
Json render_music_wav(std::shared_ptr<const MusicScore> score, const std::filesystem::path& output, double seconds = 0);
}
