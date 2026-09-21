#pragma once
#include "assets.hpp"
#include "music.hpp"
#include "model.hpp"
#include <memory>

namespace lm {
struct CompiledAssets {
    std::map<std::string, std::shared_ptr<const SpriteAsset>> sprites;
    std::map<std::string, std::shared_ptr<const MeshAsset>> meshes;
    std::map<std::string, std::shared_ptr<const ModelAsset>> models;
    std::map<std::string, std::shared_ptr<const MusicScore>> music;
};
CompiledAssets compile_assets(const Project& project);
Json inspect_asset(const Project& project, const CompiledAssets& assets, const std::string& id);
}
