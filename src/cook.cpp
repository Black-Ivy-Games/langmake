#include "langmake/cook.hpp"
#include "langmake/project.hpp"
#include "langmake/asset_runtime.hpp"
#include "langmake/world.hpp"
#include "langmake/io.hpp"
#include <chrono>
#include <set>

namespace lm {
Json cook_project(const std::filesystem::path& path, const std::filesystem::path& output,
                  const std::string& target, const std::string& profile) {
    namespace fs = std::filesystem;
    if (target != "android" && target != "windows" && target != "ios" && target != "macos")
        throw std::runtime_error("Cook target must be android, ios, macos or windows");
    const auto destination = fs::weakly_canonical(fs::absolute(output));
    if (output.empty() || destination == destination.root_path() || fs::exists(destination))
        throw std::runtime_error("Cook requires a new output directory; existing content is never overwritten");
    const auto project = load_project(path,profile);
    if (project.sources.contains("cook-manifest.json"))
        throw std::runtime_error("cook-manifest.json is reserved in a player bundle");
    const auto assets = compile_assets(project);
    World checked(project);
    for (const auto& [id, scene] : project.scenes) {
        auto candidate = *scene;
        candidate.scenes = project.scenes;
        World checked_scene(candidate);
    }
    auto staging = destination;
    staging += ".cook-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    if (fs::exists(staging)) throw std::runtime_error("Cook staging directory already exists");
    try {
        copy_project_sources(project, staging);
        // Keep the complete portable graph, including inactive profiles, while
        // making the chosen presentation explicit for players and exporters.
        if(!project.profile.empty()) {
            auto resolved=Json::parse(project.sources.at(project.file.filename().generic_string()));
            resolved.update(project.resolved_profile);
            resolved["default_profile"]=project.profile;
            if(!project.application.empty())resolved["application"]=project.application;
            write_json_atomic(staging/"project.json",resolved);
        }
        const auto packaged = load_project(staging);
        Json manifest{{"format_version",1},{"engine_version","0.5.0"},{"target",target},
            {"project_manifest","project.json"},{"source_revision",project.revision},
            {"authored_graph_revision",project.source_revision},{"profile",project.profile},
            {"resolved_profile",project.resolved_profile},{"application",project.application},{"save_structure_fingerprint",save_structure_fingerprint(project)},
            {"packaged_revision",packaged.revision},{"project_id",project.document.value("project_id",std::string())},
            {"content_encoding","validated-source-graph"},
            {"asset_loading","Native recipes are compiled by the player; this format is not a precompiled binary cache"},
            {"files",Json::array()},{"assets",Json::array()}};
        std::size_t bytes = 0;
        for (const auto& [relative, data] : packaged.sources) {
            bytes += data.size();
            manifest["files"].push_back({{"path",relative},{"bytes",data.size()},{"revision",revision_for(data)}});
        }
        for (const auto& [id, source] : project.assets)
            manifest["assets"].push_back({{"id",id},{"type",source.type},{"revision",source.revision}});
        manifest["scene_assets"]=Json::object();
        for(const auto&[scene_id,scene]:project.scenes) {
            auto& entries=manifest["scene_assets"][scene_id];entries=Json::array();
            for(const auto&[id,source]:scene->assets)
                entries.push_back({{"id",id},{"type",source.type},{"revision",source.revision}});
        }
        bool expressive_audio=false;
        std::function<void(const Json&)> scan_audio=[&](const Json& value){
            if(value.is_object()) {
                if(value.value("type",Json())=="music"&&value.value("schema_version",Json())==2)expressive_audio=true;
                if(value.value("type",Json())=="eq"||value.value("type",Json())=="compressor"||value.value("type",Json())=="limiter")expressive_audio=true;
                for(const auto& v:value)scan_audio(v);
            }else if(value.is_array())for(const auto& v:value)scan_audio(v);
        };
        scan_audio(project.document);
        for(const auto&[id,source]:project.assets)if(source.type=="music")scan_audio(source.document);
        for(const auto&[id,scene]:project.scenes) {
            scan_audio(scene->document);
            for(const auto&[asset_id,source]:scene->assets)if(source.type=="music")scan_audio(source.document);
        }
        manifest["audio_feature_revision"]=expressive_audio?1:0;
        manifest["source_bytes"] = bytes;
        manifest["runtime_content_limit_bytes"] = 256 * 1024 * 1024;
        // Revisions identify source changes. Mobile packagers add SHA-256
        // hashes for installation integrity; these are not interchangeable.
        write_json_atomic(staging / "cook-manifest.json", manifest);
        if (load_project(path,profile).revision != project.revision)
            throw std::runtime_error("Sources changed while preparing the player bundle; retry from the new revision");
        fs::rename(staging, destination);
        return {{"ok",true},{"target",target},{"output",destination.generic_string()},
            {"profile",project.profile},{"resolved_profile",project.resolved_profile},{"application",project.application},
            {"source_revision",project.revision},{"packaged_revision",packaged.revision},
            {"source_files",manifest["files"].size()},{"source_bytes",bytes},
            {"content_encoding","validated-source-graph"},{"manifest",(destination/"cook-manifest.json").generic_string()}};
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(staging, ignored);
        throw;
    }
}
}
