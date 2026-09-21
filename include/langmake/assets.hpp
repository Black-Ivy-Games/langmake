#pragma once
#include "types.hpp"
#include <cstdint>

namespace lm {
struct SpriteFrame {
    std::string id;
    int width = 0, height = 0;
    std::vector<unsigned char> rgba;
};
struct SpriteClip {
    std::vector<std::size_t> frames;
    std::vector<double> durations;
    bool loop = true;
};
struct SpriteAsset {
    std::vector<SpriteFrame> frames;
    std::map<std::string, SpriteClip> animations;
    std::string default_animation;
};
struct MeshVertex {
    Vec3 position;
    Vec3 normal;
    Color color;
};
struct MeshAsset {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    Vec3 minimum{0,0,0}, maximum{0,0,0};
    std::vector<std::string> parts;
    // One starting index per named part, followed by the total index count.
    std::vector<std::size_t> part_index_offsets;
};
SpriteAsset compile_sprite(const AssetSource& source);
MeshAsset compile_mesh(const AssetSource& source);
std::size_t sprite_frame_at(const SpriteAsset& sprite, const std::string& clip, double seconds);
Json asset_schema(const std::string& type);
Json inspect_sprite(const SpriteAsset& sprite);
Json inspect_mesh(const MeshAsset& mesh);
void write_sprite_sheet(const SpriteAsset& sprite, const std::filesystem::path& output, int scale = 1);
void write_mesh_obj(const MeshAsset& mesh, const std::filesystem::path& output);
}
