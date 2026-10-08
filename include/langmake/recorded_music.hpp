#pragma once
#include "types.hpp"
#include "audio_mixer.hpp"
#include <array>
#include <memory>
#include <string_view>
namespace lm {
// Decoded on the authoring/game thread, immutable and shared by playback voices.
struct RecordedMusic {
    struct Point { uint64_t frame; float gain, pan; };
    struct Track {
        std::string id, source;
        std::vector<int16_t> pcm;
        uint64_t offset=0;
        float gain=1, pan=0;
        std::vector<Point> automation;
        Json effects=Json::array();
    };
    std::vector<Track> tracks;
    std::vector<uint64_t> beats;
    uint64_t length=0, bytes=0;
    int beats_per_bar=4;
};
std::shared_ptr<const RecordedMusic> compile_recorded_music(const AssetSource& source);
std::vector<std::string> music_dependencies(const AssetSource& source);
Json recorded_music_schema();
class RecordedVoice {
public:
    explicit RecordedVoice(std::shared_ptr<const RecordedMusic> score);
    void reset();
    void sample(float& left,float& right,uint64_t frame,bool loop);
    bool set_layer(std::string_view id,float gain,float seconds,int quantization,uint64_t frame,bool loop);
private:
    struct Layer { float gain=1, from=1, target=1; uint64_t start=0, duration=0; bool pending=false; std::size_t point=0; };
    std::shared_ptr<const RecordedMusic> score_;
    std::array<Layer,16> layers_{};
    std::vector<std::unique_ptr<AudioEffects>> effects_;
};
}
