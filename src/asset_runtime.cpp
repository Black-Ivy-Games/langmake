#include "langmake/asset_runtime.hpp"
#include "langmake/project.hpp"
#include <mutex>

namespace lm {
namespace {
std::mutex cache_mutex;
template<class T> using Cache = std::map<std::string, std::weak_ptr<const T>>;
Cache<SpriteAsset> sprite_cache;
Cache<MeshAsset> mesh_cache;
Cache<ModelAsset> model_cache;
Cache<MusicScore> music_cache;
template<class T, class Compile>
std::shared_ptr<const T> get_cached(Cache<T>& cache, const AssetSource& source, Compile compile) {
    std::string key = source.revision;
    if (key.empty()) {
        std::string bytes = source.document.dump();
        for (const auto& [path, data] : source.inputs) bytes += path + ":" + revision_for(data);
        key = revision_for(bytes);
    }
    {
        std::lock_guard guard(cache_mutex);
        if (auto found = cache.find(key); found != cache.end()) if (auto value = found->second.lock()) return value;
    }
    auto value = compile(source);
    {
        std::lock_guard guard(cache_mutex);
        for (auto it = cache.begin(); it != cache.end();) {
            if (it->second.expired()) it = cache.erase(it); else ++it;
        }
        cache[key] = value;
    }
    return value;
}
}
CompiledAssets compile_assets(const Project& project) {
    CompiledAssets result;
    std::size_t visual_bytes = 0, music_notes = 0, music_bytes = 0;
    for (const auto& [id, source] : project.assets) {
        try {
            if (source.type == "sprite") result.sprites[id] = get_cached(sprite_cache, source, [](const AssetSource& s) { return std::make_shared<const SpriteAsset>(compile_sprite(s)); });
            else if (source.type == "mesh") result.meshes[id] = get_cached(mesh_cache, source, [](const AssetSource& s) { return std::make_shared<const MeshAsset>(compile_mesh(s)); });
            else if (source.type == "model") result.models[id] = get_cached(model_cache, source, [](const AssetSource& s) { return std::make_shared<const ModelAsset>(compile_model(s)); });
            else if (source.type == "music") result.music[id] = get_cached(music_cache, source, compile_music);
            else throw std::runtime_error("Unsupported asset type: " + source.type);
            if (source.type == "sprite") for (const auto& frame : result.sprites.at(id)->frames) visual_bytes += frame.rgba.size();
            if (source.type == "mesh") {
                const auto& mesh = *result.meshes.at(id);
                visual_bytes += mesh.vertices.size() * sizeof(MeshVertex) + mesh.indices.size() * sizeof(uint32_t);
            }
            if (source.type == "model") {
                const auto& model = *result.models.at(id);
                visual_bytes += model.animation_keys*64 + model.joints.size()*sizeof(ModelJoint);
                for (const auto& p : model.primitives) visual_bytes += p.vertices.size()*sizeof(ModelVertex)+p.indices.size()*sizeof(uint32_t)+p.inverse_bind.size()*sizeof(Matrix4);
                for (const auto& image : model.images) visual_bytes += image.rgba.size();
            }
            if (source.type == "music") {auto info=inspect_music(*result.music.at(id));music_notes+=info.at("notes").get<std::size_t>();music_bytes+=info.value("decoded_bytes",std::size_t(0));}
            if(music_bytes>192*1024*1024)throw std::runtime_error("Project recorded audio exceeds 192 MiB of decoded PCM");
            if (visual_bytes > 256 * 1024 * 1024 || music_notes > 1024 * 1024)
                throw std::runtime_error("Project compiled asset budget is 256 MiB of visual data and 1048576 music notes");
        } catch (const std::exception& e) {
            auto diagnostic = Json::parse(e.what(), nullptr, false);
            if (!diagnostic.is_object()) diagnostic = {{"code", "asset_build_failed"}, {"message", e.what()}};
            diagnostic["asset"] = id; diagnostic["file"] = source.path.generic_string();
            throw std::runtime_error(diagnostic.dump());
        }
    }
    for (const auto& entity : project.entities) if (!entity.asset.empty()) {
        if (auto sprite = result.sprites.find(entity.asset); sprite != result.sprites.end())
            sprite_frame_at(*sprite->second, entity.animation, 0);
        else if (auto model = result.models.find(entity.asset); model != result.models.end()) {
            if (!entity.animation.empty() && !model->second->animations.contains(entity.animation)) throw std::runtime_error("Unknown model animation for entity: " + entity.id);
        } else if (!result.meshes.contains(entity.asset)) throw std::runtime_error("Entity visual asset must be a sprite, mesh or model: " + entity.id);
    }
    return result;
}
Json inspect_asset(const Project& project, const CompiledAssets& assets, const std::string& id) {
    const auto found = project.assets.find(id);
    if (found == project.assets.end()) throw std::runtime_error("Unknown asset: " + id);
    Json result;
    if (found->second.type == "sprite") result = inspect_sprite(*assets.sprites.at(id));
    else if (found->second.type == "mesh") result = inspect_mesh(*assets.meshes.at(id));
    else if (found->second.type == "model") result = inspect_model(*assets.models.at(id));
    else result = inspect_music(*assets.music.at(id));
    result["ok"] = true; result["id"] = id; result["type"] = found->second.type;
    result["source_revision"] = project.revision;
    result["asset_revision"] = found->second.revision;
    result["file"] = found->second.path.generic_string();
    return result;
}
}
