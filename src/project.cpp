#include "langmake/collision2d.hpp"
#include "langmake/project.hpp"
#include "langmake/recorded_music.hpp"
#include "langmake/asset_runtime.hpp"
#include "langmake/io.hpp"
#include "langmake/model.hpp"
#include "langmake/audio_mixer.hpp"
#include "langmake/script_api.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace lm {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t max_file_bytes = 4 * 1024 * 1024;
constexpr std::size_t max_entities = 4096;
constexpr std::size_t max_assets = 256;
constexpr std::size_t max_source_bytes = 32 * 1024 * 1024;

Json failure(const std::string& code, const std::string& path, const std::string& message) {
    return {{"ok", false}, {"diagnostics", Json::array({{{"code", code}, {"path", path}, {"message", message}}})}};
}
void issue(Json& diagnostics, const std::string& code, const std::string& path, const std::string& message) {
    diagnostics.push_back({{"code", code}, {"path", path}, {"message", message}});
}
bool object_keys(const Json& object, std::initializer_list<std::string_view> allowed,
                 const std::string& path, Json& errors) {
    if (!object.is_object()) {
        issue(errors, "type", path, "Expected an object.");
        return false;
    }
    for (const auto& [key, value] : object.items()) {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            issue(errors, "unknown_field", path + "/" + key, "Unknown field '" + key + "'.");
    }
    return true;
}
void required(const Json& object, std::initializer_list<std::string_view> fields,
              const std::string& path, Json& errors) {
    for (auto field : fields)
        if (!object.contains(field)) issue(errors, "required", path + "/" + std::string(field), "Required field is missing.");
}
bool valid_text(const Json& value, std::size_t limit) {
    if (!value.is_string()) return false;
    const auto& text = value.get_ref<const std::string&>();
    return !text.empty() && text.size() <= limit &&
        std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool identifier(const std::string& value, std::size_t limit = 64) {
    return !value.empty() && value.size() <= limit && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
bool relative_path(const std::string& value, const std::string& extension = ".luau") {
    if (value.empty() || value.size() > 240 || value.find_first_of("\\:<>\"|?*") != std::string::npos ||
        std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; })) return false;
    const fs::path path(value);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& component : path) {
        auto part = component.string();
        std::transform(part.begin(), part.end(), part.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (part == ".." || part == "." || part == ".langmake" || part.empty() || part.back() == '.' || part.back() == ' ') return false;
    }
    return extension.empty() || path.extension() == extension;
}
bool numeric(const Json& value, double low, double high) {
    if (!value.is_number()) return false;
    const double number = value.get<double>();
    return std::isfinite(number) && number >= low && number <= high;
}
void vector_field(const Json& value, const std::string& path, std::size_t count,
                  double low, double high, Json& errors) {
    if (!value.is_array() || value.size() != count) {
        issue(errors, "type", path, "Expected an array of " + std::to_string(count) + " numbers.");
        return;
    }
    for (std::size_t i = 0; i < count; ++i)
        if (!numeric(value[i], low, high)) issue(errors, "range", path + "/" + std::to_string(i),
            "Expected a finite number between " + std::to_string(low) + " and " + std::to_string(high) + ".");
}
void enum_field(const Json& object, const char* key, std::initializer_list<std::string_view> values,
                const std::string& path, Json& errors) {
    if (!object.contains(key)) return;
    const auto& value = object[key];
    if (!value.is_string() || std::find(values.begin(), values.end(), value.get_ref<const std::string&>()) == values.end())
        issue(errors, "enum", path + "/" + key, "Unsupported value for '" + std::string(key) + "'.");
}
bool contained(const fs::path& root, const fs::path& candidate) {
    auto a = root.begin(), b = candidate.begin();
    for (; a != root.end(); ++a, ++b) {
        if (b == candidate.end()) return false;
#ifdef _WIN32
        auto lhs = a->wstring(), rhs = b->wstring();
        std::transform(lhs.begin(), lhs.end(), lhs.begin(), ::towlower);
        std::transform(rhs.begin(), rhs.end(), rhs.begin(), ::towlower);
        if (lhs != rhs) return false;
#else
        if (*a != *b) return false;
#endif
    }
    return true;
}
fs::path scoped(const fs::path& root, const fs::path& relative) {
    if (relative.empty() || relative.is_absolute() || relative.has_root_name()) throw std::runtime_error("Expected a project-relative path.");
    for (const auto& part : relative) if (part == "..") throw std::runtime_error("Parent traversal is not allowed in project paths.");
    const auto result = fs::weakly_canonical(root / relative);
    if (!contained(root, result)) throw std::runtime_error("Path escapes the project root: " + relative.generic_string());
    return result;
}
std::string read_file(const fs::path& path) {
    if(!fs::is_regular_file(path))throw std::runtime_error("File does not exist or is not regular: "+path.generic_string());
    auto ext=path.extension().string();auto limit=(ext==".json"||ext==".luau"||ext==".gltf"||ext==".rml"||ext==".rcss")?max_file_bytes:32*1024*1024;
    return read_shared_file(path,limit);
}
void write_file(const fs::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("Could not write file: " + path.generic_string());
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    if (!stream) throw std::runtime_error("Could not finish writing file: " + path.generic_string());
}
Json parse_document(const std::string& bytes) { return parse_json_strict(bytes); }
void ensure_valid(const Json& document) {
    const auto result = validate_document(document);
    if (!result["ok"].get<bool>()) throw std::runtime_error(result.dump());
}
std::string combined_revision(const std::string& manifest, const std::string& script) {
    return revision_for(std::to_string(manifest.size()) + ":" + manifest + std::to_string(script.size()) + ":" + script);
}
std::string unique_suffix() {
    static std::atomic<unsigned long long> counter{0};
    return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(counter++);
}
void atomic_write(const fs::path& root, const fs::path& target, const std::string& bytes,
                  const fs::path& backup = {}) {
    auto ext=target.extension().string(); const size_t limit=(ext==".json"||ext==".luau"||ext==".gltf"||ext==".rml"||ext==".rcss")?max_file_bytes:32*1024*1024;
    if(bytes.size()>limit)throw std::runtime_error("Replacement exceeds source file byte limit.");
    if (!contained(root, fs::weakly_canonical(target))) throw std::runtime_error("Write target escapes project root.");
    const auto temporary = scoped(root, fs::relative(target.parent_path(), root) / (target.filename().string() + ".tmp-" + unique_suffix()));
    fs::create_directories(target.parent_path());
    try {
        write_file(temporary, bytes);
        if (!backup.empty() && fs::exists(target)) {
            if (!contained(root, fs::weakly_canonical(backup))) throw std::runtime_error("Backup path escapes project root.");
            fs::create_directories(backup.parent_path());
            fs::copy_file(target, backup, fs::copy_options::overwrite_existing);
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Atomic file replacement failed (Windows error " + std::to_string(GetLastError()) + ").");
#else
        fs::rename(temporary, target);
#endif
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}
struct MutationLock {
    fs::path path;
    explicit MutationLock(const fs::path& root) : path(scoped(root, ".langmake/mutation.lock")) {
        fs::create_directories(path.parent_path());
        if (!fs::create_directory(path)) throw std::runtime_error("Project is locked by another mutation. If a process crashed, remove .langmake/mutation.lock after confirming it has stopped.");
    }
    ~MutationLock() { std::error_code ignored; fs::remove(path, ignored); }
    MutationLock(const MutationLock&) = delete;
    MutationLock& operator=(const MutationLock&) = delete;
};
fs::path checkpoint_path(const Project& project, const std::string& name) {
    if (!identifier(name)) throw std::runtime_error("Checkpoint name must contain 1-64 ASCII letters, digits, '_' or '-'.");
    return scoped(project.root, fs::path(".langmake/checkpoints") / name);
}
Json number_array_schema(std::size_t count, double minimum, double maximum) {
    return {{"type", "array"}, {"minItems", count}, {"maxItems", count},
            {"items", {{"type", "number"}, {"minimum", minimum}, {"maximum", maximum}}}};
}
}

std::string revision_for(const std::string& bytes) {
    // Stable content identifier, not a cryptographic integrity or trust primitive.
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }
    std::ostringstream result;
    result << "fnv1a64-" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return result.str();
}

Json validate_document(const Json& document) {
    Json errors = Json::array();
    if (!object_keys(document, {"schema_version", "dimension", "title", "gravity", "camera", "game_camera", "script", "entities", "assets", "music", "modules", "prefabs", "scenes", "entry_scene", "ui", "definitions", "input", "audio", "navigation", "state", "save_version", "project_id", "seed", "profiles", "default_profile", "application", "scenarios", "save_compatibility"}, "", errors))
        return {{"ok", false}, {"diagnostics", errors}};
    required(document, {"schema_version", "dimension", "title", "script", "entities"}, "", errors);
    if (document.contains("schema_version") && (!document["schema_version"].is_number_integer() || (document["schema_version"] != 1 && document["schema_version"] != 2)))
        issue(errors, "schema_version", "/schema_version", "Only integer schema_version 1 or 2 is supported.");
    const bool modern=document.value("schema_version",Json(1))==2;
    for(auto kind:{"modules","prefabs","scenes","definitions","scenarios"})if(document.contains(kind)) {
        const auto& registry=document[kind];
        if(!modern)issue(errors,"schema_version",std::string("/")+kind,"This registry requires schema_version 2.");
        if(!registry.is_object()||registry.size()>256)issue(errors,"registry",std::string("/")+kind,"Expected at most 256 ID-to-path registrations.");
        else for(auto&[id,path]:registry.items())if(!identifier(id)||!path.is_string()||!relative_path(path.get<std::string>(),std::string(kind)=="modules"?".luau":".json"))issue(errors,"registry",std::string("/")+kind+"/"+id,"Expected a stable ID and safe source path.");
    }
    if(document.contains("entry_scene")&&(!document["entry_scene"].is_string()||!document.value("scenes",Json::object()).contains(document["entry_scene"].get<std::string>())))issue(errors,"scene_reference","/entry_scene","Entry scene must be registered.");
    for(auto key:{"profiles","default_profile","application","save_compatibility"})if(document.contains(key)&&!modern)
        issue(errors,"schema_version",std::string("/")+key,"This field requires schema_version 2.");
    if(document.contains("application")) {
        const auto& app=document["application"];
        auto app_fields=[&](const Json& value,const std::string& path,bool platform) {
            if(!object_keys(value,platform?std::initializer_list<std::string_view>{"id","icon","orientation"}:
                std::initializer_list<std::string_view>{"id","label","version","build","icon","orientation","platforms"},path,errors))return;
            if(value.contains("id")) {
                const auto& id=value["id"];
                if(!id.is_string()||!valid_text(id,255)||id.get_ref<const std::string&>().find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-")!=std::string::npos||
                   id.get_ref<const std::string&>().front()=='.'||id.get_ref<const std::string&>().back()=='.'||id.get_ref<const std::string&>().find("..")!=std::string::npos)
                    issue(errors,"application_id",path+"/id","Expected a portable application identifier; native tools also validate platform rules.");
            }
            if(value.contains("icon")&&(!value["icon"].is_string()||!relative_path(value["icon"].get<std::string>(),".png")))
                issue(errors,"path",path+"/icon","Expected a project-relative PNG icon.");
            enum_field(value,"orientation",{"portrait","landscape","sensor"},path,errors);
            if(value.contains("label")&&!valid_text(value["label"],160))issue(errors,"type",path+"/label","Expected a nonempty application label up to 160 bytes.");
            if(value.contains("version")&&(!valid_text(value["version"],64)||value["version"].get_ref<const std::string&>().find_first_not_of("0123456789.")!=std::string::npos||
                value["version"].get_ref<const std::string&>().front()=='.'||value["version"].get_ref<const std::string&>().back()=='.'||value["version"].get_ref<const std::string&>().find("..")!=std::string::npos))
                issue(errors,"version",path+"/version","Expected a dotted numeric version string up to 64 bytes.");
            if(value.contains("build")&&(!value["build"].is_number_integer()||!numeric(value["build"],1,2100000000)))issue(errors,"range",path+"/build","Expected integer build 1..2100000000.");
        };
        app_fields(app,"/application",false);
        if(app.is_object()&&app.contains("platforms")&&object_keys(app["platforms"],{"android","ios","macos","windows"},"/application/platforms",errors))
            for(const auto&[platform,value]:app["platforms"].items())app_fields(value,"/application/platforms/"+platform,true);
    }
    if(document.contains("save_compatibility")) {
        const auto& contract=document["save_compatibility"];
        if(object_keys(contract,{"id","allow_presentation_changes"},"/save_compatibility",errors)) {
            required(contract,{"id","allow_presentation_changes"},"/save_compatibility",errors);
            if(!contract.contains("id")||!contract["id"].is_string()||!identifier(contract["id"].get<std::string>()))issue(errors,"id","/save_compatibility/id","Expected a stable assessed compatibility ID.");
            if(contract.contains("allow_presentation_changes")&&!contract["allow_presentation_changes"].is_boolean())issue(errors,"type","/save_compatibility/allow_presentation_changes","Expected an explicit boolean declaration.");
            if(!document.contains("project_id"))issue(errors,"required","/project_id","Save compatibility requires an explicit stable project_id.");
        }
    }
    if(document.contains("profiles")) {
        const auto& profiles=document["profiles"];
        if(!profiles.is_object()||profiles.size()>32)issue(errors,"profiles","/profiles","Expected at most 32 named presentation profiles.");
        else for(const auto&[name,overrides]:profiles.items()) {
            const auto at="/profiles/"+name;
            if(!identifier(name))issue(errors,"id",at,"Invalid profile ID.");
            if(!object_keys(overrides,{"input","ui","camera","game_camera","application"},at,errors))continue;
            Json candidate=document;candidate.erase("profiles");candidate.erase("default_profile");candidate.update(overrides);
            // Unknown override keys are never applied, including gameplay geometry.
            if(std::all_of(overrides.items().begin(),overrides.items().end(),[](const auto& item){const auto& k=item.key();return k=="input"||k=="ui"||k=="camera"||k=="game_camera"||k=="application";})) {
                const auto checked=validate_document(candidate);
                for(auto diagnostic:checked["diagnostics"]) { diagnostic["path"]=at+diagnostic.value("path",std::string());errors.push_back(std::move(diagnostic)); }
            }
        }
    }
    if(document.contains("default_profile")&&(!document["default_profile"].is_string()||!document.value("profiles",Json::object()).contains(document["default_profile"].get<std::string>())))
        issue(errors,"profile_reference","/default_profile","Default profile must name an explicitly registered profile.");
    for(auto key:{"ui","input","navigation","state","game_camera"})if(document.contains(key)&&!document[key].is_object())issue(errors,"type",std::string("/")+key,"Expected an object.");
    if(document.contains("ui")&&document["ui"].is_object()&&document["ui"].contains("expected_characters")&&
       (!document["ui"]["expected_characters"].is_string()||document["ui"]["expected_characters"].get_ref<const std::string&>().size()>16384))
        issue(errors,"type","/ui/expected_characters","Expected a string up to 16384 bytes for dynamic glyph coverage.");
    if(document.contains("input")&&document["input"].is_object()) {
        const auto& input=document["input"];
        if(object_keys(input,{"actions","touch"},"/input",errors)&&input.contains("actions")){
            if(!input["actions"].is_object()||input["actions"].size()>64)issue(errors,"input","/input/actions","Expected at most 64 action bindings.");
            else for(const auto&[name,binding]:input["actions"].items()){
                const auto path="/input/actions/"+name;
                if(!identifier(name))issue(errors,"id",path,"Invalid action name.");
                if(!object_keys(binding,{"keys","positive","negative","gamepad","gamepad_axis"},path,errors))continue;
                for(auto key:{"keys","positive","negative"})if(binding.contains(key)){
                    const auto& keys=binding[key];
                    if(!keys.is_array()||keys.size()>16||!std::all_of(keys.begin(),keys.end(),[](const Json& key){return valid_text(key,64);}))issue(errors,"input",path+"/"+key,"Expected up to 16 SDL key names.");
                }
                enum_field(binding,"gamepad",{"south","east","west","north","a","b","x","y","start","back","guide","leftstick","rightstick","leftshoulder","rightshoulder","dpup","dpdown","dpleft","dpright"},path,errors);
                enum_field(binding,"gamepad_axis",{"leftx","lefty","rightx","righty","lefttrigger","righttrigger"},path,errors);
            }
        }
        if(input.contains("touch") && object_keys(input["touch"],{"enabled","stick","buttons"},"/input/touch",errors)) {
            const auto& touch=input["touch"];
            const auto& actions=input.contains("actions")&&input["actions"].is_object()?input["actions"]:Json::object();
            auto known=[&](const Json& value,bool axis) {
                if(!value.is_string())return false;
                const auto name=value.get<std::string>();
                return actions.contains(name)||(axis?(name=="move_x"||name=="move_z"):(name=="jump"||name=="fire"));
            };
            if(touch.contains("enabled")&&!touch["enabled"].is_boolean())issue(errors,"type","/input/touch/enabled","Expected boolean.");
            if(touch.contains("stick")&&object_keys(touch["stick"],{"enabled","x_action","y_action"},"/input/touch/stick",errors)) {
                const auto& stick=touch["stick"];
                if(stick.contains("enabled")&&!stick["enabled"].is_boolean())issue(errors,"type","/input/touch/stick/enabled","Expected boolean.");
                for(auto key:{"x_action","y_action"})if(stick.contains(key)&&!known(stick[key],true))
                    issue(errors,"input_reference",std::string("/input/touch/stick/")+key,"Expected registered action or legacy move_x/move_z axis.");
                if((!stick.contains("x_action")||stick["x_action"].is_string())&&(!stick.contains("y_action")||stick["y_action"].is_string())&&
                    stick.value("x_action",std::string("move_x"))==stick.value("y_action",std::string("move_z")))
                    issue(errors,"input","/input/touch/stick","Joystick axes must use distinct actions.");
            }
            if(touch.contains("buttons")) {
                const auto& buttons=touch["buttons"];
                if(!buttons.is_array()||buttons.size()>64)issue(errors,"input","/input/touch/buttons","Expected at most 64 ordered buttons.");
                else {std::set<std::string> seen;
                    for(std::size_t i=0;i<buttons.size();++i) {
                        const auto& button=buttons[i];const auto at="/input/touch/buttons/"+std::to_string(i);
                        if(!object_keys(button,{"action","label"},at,errors))continue;
                        if(!button.contains("action")||!known(button["action"],false))issue(errors,"input_reference",at+"/action","Expected registered action or legacy jump/fire button.");
                        else if(!seen.insert(button["action"].get<std::string>()).second)issue(errors,"duplicate",at+"/action","Touch actions must be unique.");
                        if(button.contains("label")&&!valid_text(button["label"],24))issue(errors,"type",at+"/label","Expected a nonempty label of at most 24 bytes.");
                    }
                }
            }
        }
    }
    if(document.contains("audio"))try{validate_audio_mixer(document["audio"]);}catch(const std::exception&e){issue(errors,"audio","/audio",e.what());}
    if(document.contains("project_id")&&(!document["project_id"].is_string()||!identifier(document["project_id"].get<std::string>())))issue(errors,"id","/project_id","Expected a stable game identifier.");
    if(document.contains("save_version")&&(!document["save_version"].is_number_integer()||!numeric(document["save_version"],1,1000000)))issue(errors,"range","/save_version","Expected integer 1..1000000.");
    if(document.contains("seed")&&(!document["seed"].is_number_integer()||!numeric(document["seed"],0,9007199254740991.0)))issue(errors,"range","/seed","Expected an integer random seed in 0..2^53-1.");
    enum_field(document, "dimension", {"2d", "3d"}, "", errors);
    if (document.contains("title") && !valid_text(document["title"], 160))
        issue(errors, "type", "/title", "Expected a nonempty title of at most 160 bytes without control characters.");
    if (document.contains("script") && (!document["script"].is_string() || !relative_path(document["script"].get<std::string>())))
        issue(errors, "path", "/script", "Expected a relative .luau file path using '/', without absolute paths, backslashes or parent traversal.");
    if (document.contains("gravity")) vector_field(document["gravity"], "/gravity", 3, -1000, 1000, errors);
    std::map<std::string, std::string> asset_types;
    if (document.contains("assets")) {
        const auto& assets = document["assets"];
        if (!assets.is_object() || assets.size() > max_assets) issue(errors, "type", "/assets", "Expected an object with at most 256 named assets.");
        else {
            std::set<std::string> paths;
            for (const auto& [id, asset] : assets.items()) {
                const auto location = "/assets/" + id;
                if (!identifier(id)) issue(errors, "id", location, "Asset IDs must contain 1-64 ASCII letters, digits, '_' or '-'.");
                if (!object_keys(asset, {"type", "path"}, location, errors)) continue;
                required(asset, {"type", "path"}, location, errors);
                enum_field(asset, "type", {"sprite", "mesh", "music", "model"}, location, errors);
                if (asset.contains("type") && asset["type"].is_string()) asset_types[id] = asset["type"].get<std::string>();
                if (!asset.contains("path") || !asset["path"].is_string() || !relative_path(asset["path"].get<std::string>(), ".json"))
                    issue(errors, "path", location + "/path", "Expected a project-relative .json asset recipe path without traversal.");
                else {
                    auto key = asset["path"].get<std::string>();
                    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (!paths.insert(key).second) issue(errors, "duplicate_path", location + "/path", "Each asset must have its own recipe path, including letter case.");
                }
            }
        }
    }
    if (document.contains("music")) {
        const auto& music = document["music"];
        if (!music.is_string() || !identifier(music.get<std::string>())) issue(errors, "id", "/music", "Expected a registered music asset ID.");
        else if (!asset_types.contains(music.get<std::string>()) || asset_types[music.get<std::string>()] != "music")
            issue(errors, "asset_reference", "/music", "Background music must reference an asset of type 'music'.");
    }
    if (document.contains("camera") && object_keys(document["camera"], {"position", "target", "mode", "follow", "offset", "smoothing", "half_height", "fov", "distance", "target_entity"}, "/camera", errors)) {
        const auto& camera = document["camera"];
        required(camera, {"position", "target"}, "/camera", errors);
        for (const auto* key : {"position", "target"})
            if (camera.contains(key)) vector_field(camera[key], "/camera/" + std::string(key), 3, -100000, 100000, errors);
        if (camera.contains("position") && camera.contains("target") && camera["position"] == camera["target"])
            issue(errors, "camera", "/camera", "Camera position and target must differ.");
    }
    if (document.contains("entities")) {
        const auto& entities = document["entities"];
        if (!entities.is_array() || (!modern && entities.empty()) || entities.size() > max_entities)
            issue(errors, "type", "/entities", "Expected between 1 and 4096 entities.");
        else {
            std::set<std::string> ids;
            int players = 0;
            for (std::size_t i = 0; i < entities.size(); ++i) {
                const auto& entity = entities[i];
                const auto path = "/entities/" + std::to_string(i);
                if (!object_keys(entity, {"id", "name", "shape", "body", "role", "position", "size", "color", "mass", "visible", "asset", "animation", "rotation", "flip_x", "animation_speed", "visual_scale", "collision_enabled", "controller", "parent", "parent_joint", "physics2d"}, path, errors)) continue;
                required(entity, {"id", "position", "size"}, path, errors);
                if (entity.contains("id")) {
                    if (!entity["id"].is_string() || !identifier(entity["id"].get<std::string>()))
                        issue(errors, "id", path + "/id", "ID must contain 1-64 ASCII letters, digits, '_' or '-'.");
                    else if (!ids.insert(entity["id"].get<std::string>()).second)
                        issue(errors, "duplicate_id", path + "/id", "Entity IDs must be unique.");
                }
                if (entity.contains("name") && !valid_text(entity["name"], 160))
                    issue(errors, "type", path + "/name", "Expected a nonempty name of at most 160 bytes without control characters.");
                enum_field(entity, "shape", {"box", "sphere", "capsule"}, path, errors);
                enum_field(entity, "body", {"static", "dynamic", "none", "kinematic", "trigger"}, path, errors);
                if(!modern)enum_field(entity,"role",{"player","collectible","goal","hazard","decoration"},path,errors);
                else if(entity.contains("role")&&(!entity["role"].is_string()||!identifier(entity["role"].get<std::string>())))issue(errors,"id",path+"/role","Expected role ID.");
                if(entity.contains("physics2d")) {
                    try { validate_collision2d(entity["physics2d"]);
                        if(!entity["physics2d"].empty()&&(document.value("dimension","")!="2d"||entity.value("controller",Json(""))!=""||entity.value("parent",Json(""))!=""))throw std::runtime_error("physics2d requires unparented 2D bodies without controllers");
                        if(entity.contains("rotation")&&entity["rotation"].is_array()&&entity["rotation"].size()==3&&(entity["rotation"][0]!=0||entity["rotation"][1]!=0))throw std::runtime_error("physics2d only supports Z rotation");
                    }catch(const std::exception& e){issue(errors,"physics2d",path+"/physics2d",e.what());}
                }
                if(entity.contains("visual_scale"))vector_field(entity["visual_scale"],path+"/visual_scale",3,.001,1000,errors);
                if(entity.contains("collision_enabled")&&!entity["collision_enabled"].is_boolean())issue(errors,"type",path+"/collision_enabled","Expected boolean.");
                if(entity.contains("parent")&&(!entity["parent"].is_string()||(!entity["parent"].get<std::string>().empty()&&!identifier(entity["parent"].get<std::string>()))))issue(errors,"id",path+"/parent","Expected parent entity ID.");
                if(entity.contains("parent")&&entity["parent"].is_string()&&!entity["parent"].get<std::string>().empty()&&entity.value("body",Json("static"))!="none")issue(errors,"parent",path+"/body","Parented visual parts require body none.");
                if(entity.contains("parent_joint")){
                    if(!entity["parent_joint"].is_string()||(!entity["parent_joint"].get<std::string>().empty()&&!valid_text(entity["parent_joint"],128)))issue(errors,"joint",path+"/parent_joint","Expected printable joint name up to 128 bytes.");
                    if(entity.value("parent",Json(""))=="")issue(errors,"joint",path+"/parent_joint","Joint attachment requires parent entity.");
                }
                enum_field(entity,"controller",{"character",""},path,errors);
                if(entity.value("controller",Json(""))=="character") {
                    if(entity.value("body",Json("static"))!="dynamic"||entity.value("shape",Json("box"))!="capsule")issue(errors,"controller",path,"Character requires dynamic capsule body.");
                    if(entity.contains("size")&&entity["size"].is_array()&&entity["size"].size()==3&&std::all_of(entity["size"].begin(),entity["size"].end(),[](const Json&v){return numeric(v,.01,10000);})) {
                        const auto size=entity["size"].get<Vec3>();if(size[1]<size[0]||size[0]!=size[2])issue(errors,"controller",path+"/size","Capsule requires size y >= x = z.");
                    }
                }
                if (entity.contains("position")) vector_field(entity["position"], path + "/position", 3, -100000, 100000, errors);
                if (entity.contains("size")) vector_field(entity["size"], path + "/size", 3, 0.01, 10000, errors);
                if (entity.contains("color")) vector_field(entity["color"], path + "/color", 4, 0, 1, errors);
                if (entity.contains("mass") && !numeric(entity["mass"], 0.001, 100000))
                    issue(errors, "range", path + "/mass", "Mass must be finite and between 0.001 and 100000.");
                if (entity.contains("visible") && !entity["visible"].is_boolean())
                    issue(errors, "type", path + "/visible", "Expected true or false.");
                if (entity.contains("rotation")) vector_field(entity["rotation"], path + "/rotation", 3, -360000, 360000, errors);
                if (entity.contains("flip_x") && !entity["flip_x"].is_boolean()) issue(errors, "type", path + "/flip_x", "Expected true or false.");
                if (entity.contains("animation_speed") && !numeric(entity["animation_speed"], 0.001, 1000))
                    issue(errors, "range", path + "/animation_speed", "Animation speed must be finite and between 0.001 and 1000.");
                std::string asset_type;
                if (entity.contains("asset")) {
                    if (!entity["asset"].is_string() || (!entity["asset"].get<std::string>().empty() && !identifier(entity["asset"].get<std::string>())))
                        issue(errors, "id", path + "/asset", "Expected a registered sprite/mesh asset ID, or an empty string for primitives.");
                    else if (!entity["asset"].get<std::string>().empty()) {
                        const auto id = entity["asset"].get<std::string>();
                        if (!asset_types.contains(id) || (asset_types[id] != "sprite" && asset_types[id] != "mesh" && asset_types[id] != "model"))
                            issue(errors, "asset_reference", path + "/asset", "Entity visuals must reference a registered sprite or mesh asset.");
                        else asset_type = asset_types[id];
                    }
                }
                if (entity.contains("animation")) {
                    if (!entity["animation"].is_string() || (!entity["animation"].get<std::string>().empty() && !(asset_type=="model"?valid_text(entity["animation"],128):identifier(entity["animation"].get<std::string>()))))
                        issue(errors, "id", path + "/animation", "Expected an animation ID or an empty string for the default animation.");
                    else if (!entity["animation"].get<std::string>().empty() && asset_type != "sprite" && asset_type != "model")
                        issue(errors, "asset_reference", path + "/animation", "A named animation requires a sprite asset.");
                }
                if (!modern && asset_type == "mesh" && entity.contains("color") && entity["color"].is_array() && entity["color"].size() == 4 &&
                    numeric(entity["color"][3], 0, 1) && entity["color"][3].get<double>() != 1)
                    issue(errors, "mesh_alpha", path + "/color/3", "Mesh entity tint must be opaque (alpha 1).");
                if (entity.contains("shape") && entity["shape"] == "sphere" &&
                    (!entity.contains("body") || entity["body"] != "none") && entity.contains("size")) {
                    const auto& size = entity["size"];
                    if (size.is_array() && size.size() == 3 && std::all_of(size.begin(), size.end(), [](const Json& axis) { return numeric(axis, 0.01, 10000); }) &&
                        (size[0].get<float>() != size[1].get<float>() || size[0].get<float>() != size[2].get<float>()))
                        issue(errors, "sphere_size", path + "/size", "Collidable spheres require equal x, y and z sizes; body='none' permits stretched visual shapes.");
                }
                if (entity.contains("role") && entity["role"] == "player") {
                    ++players;
                    if (!entity.contains("body") || entity["body"] != "dynamic")
                        issue(errors, "player_body", path + "/body", "The player must have a dynamic body.");
                }
            }
            if (players > 1) issue(errors, "player_count", "/entities", "This alpha supports at most one player entity.");
            if(errors.empty()) {
                std::map<std::string,std::string> parents;
                for(const auto& entity:entities)parents[entity["id"].get<std::string>()]=entity.value("parent",std::string());
                for(const auto&[id,parent]:parents){std::set<std::string> visited{id};auto cursor=parent;
                    while(!cursor.empty()){
                        if(!parents.contains(cursor)){issue(errors,"parent","/entities","Unknown parent '"+cursor+"' for '"+id+"'.");break;}
                        if(!visited.insert(cursor).second){issue(errors,"parent_cycle","/entities","Parent cycle containing '"+id+"'.");break;}
                        if(visited.size()>17){issue(errors,"parent_depth","/entities","Entity hierarchy exceeds 16 ancestors.");break;}
                        cursor=parents.at(cursor);
                    }
                }
            }
        }
    }
    return {{"ok", errors.empty()}, {"diagnostics", errors}};
}

namespace {
using SourceFiles = std::map<std::string, std::string>;
using SourceReader = std::function<std::string(const std::string&)>;
struct SourceBundle { Project project; SourceFiles files; };

std::string files_revision(const SourceFiles& files, const std::string& tag = "project-sources-v2") {
    std::string bytes = tag;
    for (const auto& [path, content] : files) {
        bytes += std::to_string(path.size()) + ":" + path;
        bytes += std::to_string(content.size()) + ":" + content;
    }
    return revision_for(bytes);
}
std::pair<fs::path, fs::path> project_location(const fs::path& path) {
    auto file = fs::absolute(path);
    if (fs::is_directory(file)) file /= "project.json";
    const auto root = fs::weakly_canonical(file.parent_path());
    if (!relative_path(file.filename().generic_string(), ".json")) throw std::runtime_error("Project manifest must have a safe .json filename.");
    return {root, scoped(root, file.filename())};
}
void populate_entities(Project& project,const Json& doc) {
    for (const auto& value : doc["entities"]) {
        Entity entity;
        entity.id = value["id"].get<std::string>();
        entity.name = value.value("name", entity.id);entity.parent=value.value("parent",std::string());entity.parent_joint=value.value("parent_joint",std::string());
        entity.shape = value.value("shape", entity.shape);
        entity.body = value.value("body", entity.body);
        entity.role = value.value("role", entity.role);
        entity.position = value["position"].get<Vec3>();
        entity.size = value["size"].get<Vec3>();
        entity.asset = value.value("asset", "");
        entity.animation = value.value("animation", "");
        entity.flip_x = value.value("flip_x", false);
        entity.animation_speed = value.value("animation_speed", 1.0f);
        if (value.contains("rotation")) entity.rotation = value["rotation"].get<Vec3>();
        if (!entity.asset.empty()) entity.color = {1, 1, 1, 1};
        if (value.contains("color")) entity.color = value["color"].get<Color>();
        entity.mass = value.value("mass", entity.mass);
        entity.visible = value.value("visible", entity.visible);
        if(value.contains("visual_scale"))entity.visual_scale=value["visual_scale"].get<Vec3>();
        entity.physics2d=value.value("physics2d",Json::object());
        entity.collision_enabled=value.value("collision_enabled",true);entity.controller=value.value("controller","");
        if (!entity.animation.empty() && project.assets.at(entity.asset).type=="sprite") {
            const auto& recipe = project.assets.at(entity.asset).document;
            const bool named = recipe.contains("animations") && recipe["animations"].is_object() && recipe["animations"].contains(entity.animation);
            if (!named && (recipe.contains("animations") || entity.animation != "default"))
                throw std::runtime_error("Entity '" + entity.id + "' references unknown sprite animation '" + entity.animation + "'.");
        }
        project.entities.push_back(std::move(entity));
    }
 }
SourceBundle read_bundle(const fs::path& root, const fs::path& file, const SourceReader& reader, const std::string& selected_profile = "") {
    SourceBundle bundle;
    auto& project = bundle.project;
    project.root = root;
    project.file = file;
    std::size_t total = 0;
    std::map<std::string, std::string> resolved_paths;
    auto read = [&](const std::string& path) -> const std::string& {
        if (!relative_path(path, "")) throw std::runtime_error("Invalid project source path: " + path);
        if (const auto found = bundle.files.find(path); found != bundle.files.end()) return found->second;
        if(bundle.files.size()>=2048)throw std::runtime_error("Project source graph exceeds 2048 registered files.");
        auto resolved = scoped(root, path).generic_string();
        std::transform(resolved.begin(), resolved.end(), resolved.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (const auto found = resolved_paths.find(resolved); found != resolved_paths.end() && found->second != path)
            throw std::runtime_error("Multiple source paths alias the same file: " + path + " and " + found->second);
        resolved_paths[resolved] = path;
        auto bytes = reader(path);
        auto ext=fs::path(path).extension().string();auto limit=(ext==".json"||ext==".luau"||ext==".gltf"||ext==".rml"||ext==".rcss")?max_file_bytes:32*1024*1024;
        if (bytes.size() > limit) throw std::runtime_error("Source file exceeds its bounded limit: " + path);
        auto graph_limit=(project.document.is_object()&&project.document.value("schema_version",Json(1))==2)?256*1024*1024:max_source_bytes;
        if (bytes.size() > graph_limit - total) throw std::runtime_error("Project sources exceed the total byte limit.");
        total += bytes.size();
        return bundle.files.emplace(path, std::move(bytes)).first->second;
    };
    const auto manifest_name = file.filename().generic_string();
    project.document = parse_document(read(manifest_name));
    ensure_valid(project.document);
    const auto authored=project.document;
    project.profile=selected_profile.empty()?authored.value("default_profile",std::string()):selected_profile;
    if(!project.profile.empty()) {
        if(!authored.contains("profiles")||!authored["profiles"].contains(project.profile))throw std::runtime_error("Unknown presentation profile: "+project.profile);
        project.resolved_profile=authored["profiles"][project.profile];
        project.document.update(project.resolved_profile);
        ensure_valid(project.document);
    }
    auto read_ui=[&](const Json& ui) {
        if(!ui.is_object())throw std::runtime_error("UI configuration must be an object");
        for(auto kind:{"documents","styles","fonts","images"})if(ui.contains(kind)) {
            if(!ui[kind].is_object()||ui[kind].size()>128)throw std::runtime_error("UI source registry must contain at most 128 paths");
            for(const auto&[id,path]:ui[kind].items()){if(!identifier(id)||!path.is_string())throw std::runtime_error("Invalid UI registration");read(path.get<std::string>());}
        }
    };
    auto read_application=[&](const Json& app) {
        if(app.contains("icon"))read(app["icon"].get<std::string>());
        if(app.contains("platforms"))for(const auto&[id,platform]:app["platforms"].items())if(platform.contains("icon"))read(platform["icon"].get<std::string>());
    };
    read_ui(authored.value("ui",Json::object()));read_application(authored.value("application",Json::object()));
    if(authored.contains("profiles"))for(const auto&[id,profile]:authored["profiles"].items()) {
        read_ui(profile.value("ui",Json::object()));read_application(profile.value("application",Json::object()));
    }
    if(authored.contains("scenarios"))for(const auto&[id,path]:authored["scenarios"].items()) {
        auto scenario=parse_document(read(path.get<std::string>()));
        if(!scenario.is_object())throw std::runtime_error("Scenario must be a JSON object: "+id);
        if(scenario.contains("input")&&scenario["input"].is_string()) {
            const auto input=scenario["input"].get<std::string>();
            if(!relative_path(input,".json"))throw std::runtime_error("Scenario input must reference project-relative JSON: "+id);
            if(!parse_document(read(input)).is_object())throw std::runtime_error("Scenario input JSON must be an object: "+id);
        }
        project.scenarios[id]=std::move(scenario);
    }
    const auto& doc = project.document;
    project.dimension = doc["dimension"].get<std::string>();
    project.title = doc["title"].get<std::string>();
    const auto script_path = doc["script"].get<std::string>();
    project.script_file = scoped(root, script_path);
    project.script_source = read(script_path);
    project.music = doc.value("music", "");
    project.application=doc.value("application",Json::object());
    if(doc.contains("application")) {
        if(!project.application.contains("label"))project.application["label"]=project.title;
        if(!project.application.contains("version"))project.application["version"]="1.0";
        if(!project.application.contains("build"))project.application["build"]=1;
    }
    if (doc.contains("gravity")) project.gravity = doc["gravity"].get<Vec3>();
    if (doc.contains("camera")) {
        project.camera_position = doc["camera"]["position"].get<Vec3>();
        project.camera_target = doc["camera"]["target"].get<Vec3>();
    }
    auto read_assets = [&](Project& target, const Json& registrations) {
      target.assets.clear();
      for (const auto& [id, registration] : registrations.items()) {
        AssetSource source;
        source.type = registration["type"].get<std::string>();
        const auto asset_path = registration["path"].get<std::string>();
        if (asset_path == manifest_name) throw std::runtime_error("An asset recipe cannot also be the project manifest.");
        source.path = scoped(root, asset_path);
        source.bytes = read(asset_path);
        source.document = parse_document(source.bytes);
        if (!source.document.is_object()) throw std::runtime_error("Asset recipe must be a JSON object: " + asset_path);
        SourceFiles dependency_files{{asset_path, source.bytes}};
        if (source.type == "sprite" && source.document.contains("source_png")) {
            const auto& value = source.document["source_png"];
            if (!value.is_string() || !relative_path(value.get<std::string>(), ".png"))
                throw std::runtime_error("Sprite source_png must be a safe project-relative .png path: " + asset_path);
            const auto input = value.get<std::string>();
            source.inputs[input] = read(input);
            dependency_files[input] = source.inputs[input];
        }
        if(source.type=="model")for(int pass=0;pass<3;++pass){bool added=false;for(auto& input:model_dependencies(source))if(!source.inputs.contains(input)){source.inputs[input]=read(input);dependency_files[input]=source.inputs[input];added=true;}if(!added)break;}
        if(source.type=="music")for(auto& input:music_dependencies(source)){source.inputs[input]=read(input);dependency_files[input]=source.inputs[input];}
        source.revision = files_revision(dependency_files, "asset-" + source.type);
        target.assets.emplace(id, std::move(source));
      }
    };
    read_assets(project,doc.value("assets",Json::object()));
    populate_entities(project,doc);
    if(doc.contains("modules"))for(auto&[id,path]:doc["modules"].items()){auto name=path.get<std::string>();project.modules[id]=read(name);project.module_files[id]=scoped(root,name);}
    if(doc.contains("prefabs"))for(auto&[id,path]:doc["prefabs"].items()) {
        auto prefab=parse_document(read(path.get<std::string>()));
        auto candidate=doc;candidate["entities"]=prefab.contains("entities")?prefab["entities"]:Json::array({prefab});
        ensure_valid(candidate);project.prefabs[id]=prefab;
    }
    if(doc.contains("definitions"))for(auto&[id,path]:doc["definitions"].items())project.definitions[id]=parse_document(read(path.get<std::string>()));
    project.ui=doc.value("ui",Json::object());
    read_ui(project.ui);
    // Account for each resolved catalog rather than multiplying the complete
    // union graph: a scene with its own registry does not copy another game's
    // asset inputs. The separate full source-map copy guard remains below.
    auto catalog_bytes=[](const Project& p) {
        std::size_t bytes=p.script_source.size()+p.document.dump().size()+p.definitions.dump().size()+p.prefabs.dump().size()+p.ui.dump().size();
        for(const auto&[name,source]:p.modules)bytes+=source.size();
        for(const auto&[id,asset]:p.assets) {
            bytes+=asset.bytes.size()+asset.document.dump().size();
            for(const auto&[path,source]:asset.inputs)bytes+=source.size();
        }
        return bytes;
    };
    std::size_t resident_catalog_bytes=catalog_bytes(project);
    if(doc.contains("scenes"))for(auto&[id,path]:doc["scenes"].items()) {
        auto local=parse_document(read(path.get<std::string>()));
        Json scene_errors=Json::array();object_keys(local,{"entities","script","camera","game_camera","gravity","music","audio","navigation","state","ui","assets","modules","prefabs","definitions"},"/scenes/"+id,scene_errors);
        if(!scene_errors.empty())throw std::runtime_error(Json({{"diagnostics",scene_errors}}).dump());
        read_ui(local.value("ui",Json::object()));
        Json merged=doc;merged.update(local);merged.update(project.resolved_profile);ensure_valid(merged);
        auto child=std::make_shared<Project>(project);child->scenes.clear();child->document=merged;child->scene_id=id;
        // Registries replace the complete root registry, just like other scene
        // fields. Each scene retains the usual asset and source safety limits.
        if(local.contains("assets"))read_assets(*child,merged["assets"]);
        if(local.contains("modules")) {
            child->modules.clear();child->module_files.clear();
            for(const auto&[name,path]:merged["modules"].items()) {
                auto relative=path.get<std::string>();child->modules[name]=read(relative);child->module_files[name]=scoped(root,relative);
            }
        }
        if(local.contains("definitions")) {
            child->definitions.clear();
            for(const auto&[name,path]:merged["definitions"].items())child->definitions[name]=parse_document(read(path.get<std::string>()));
        }
        if(local.contains("prefabs")) {
            child->prefabs.clear();
            for(const auto&[name,path]:merged["prefabs"].items()) {
                auto prefab=parse_document(read(path.get<std::string>()));
                auto candidate=merged;candidate["entities"]=prefab.contains("entities")?prefab["entities"]:Json::array({prefab});
                ensure_valid(candidate);child->prefabs[name]=std::move(prefab);
            }
        }
        child->entities.clear();populate_entities(*child,merged);
        child->script_file=scoped(root,merged["script"].get<std::string>());child->script_source=read(merged["script"].get<std::string>());
        child->gravity=merged.value("gravity",Json(project.gravity)).get<Vec3>();
        child->music=merged.value("music",std::string());
        if(merged.contains("camera")){child->camera_position=merged["camera"]["position"].get<Vec3>();child->camera_target=merged["camera"]["target"].get<Vec3>();}
        child->ui=merged.value("ui",Json::object());
        read_ui(child->ui);
        resident_catalog_bytes+=catalog_bytes(*child);
        if(resident_catalog_bytes>256ull*1024*1024)throw std::runtime_error("Scene source catalog exceeds resident budget; reduce scene count or asset sizes.");
        project.scenes[id]=std::move(child);
    }
    if(doc.contains("entry_scene")) {
        auto scenes=project.scenes;auto selected=*scenes.at(doc["entry_scene"].get<std::string>());
        project=std::move(selected);project.scenes=std::move(scenes);
    }
    // Project catalogs currently hold immutable source copies per scene. Bound the
    // resident expansion explicitly until catalogs use shared source storage.
    if(total*(project.scenes.size()+1)>512ull*1024*1024)throw std::runtime_error("Scene source catalog exceeds 512 MiB resident budget; reduce scene count or shared asset size.");
    project.sources=bundle.files;
    project.source_revision=files_revision(bundle.files);
    project.revision=project.profile.empty()?project.source_revision:revision_for("resolved-profile-v1:"+project.source_revision+":"+project.profile);
    for(auto&[id,scene]:project.scenes){scene->revision=project.revision;scene->source_revision=project.source_revision;scene->sources=bundle.files;}
    project.save_fingerprint=save_structure_fingerprint(project);
    for(auto&[id,scene]:project.scenes)scene->save_fingerprint=project.save_fingerprint;
    return bundle;
}
SourceBundle disk_bundle(const fs::path& path, const SourceFiles& overrides = {}, const std::string& profile = "") {
    const auto [root, file] = project_location(path);
    return read_bundle(root, file, [&](const std::string& relative) {
        if (const auto found = overrides.find(relative); found != overrides.end()) return found->second;
        return read_file(scoped(root, relative));
    },profile);
}
void check_unchanged(const fs::path& path, const std::string& revision, const std::string& profile = "") {
    if (disk_bundle(path,{},profile).project.revision != revision) throw std::runtime_error("Project sources changed during the operation; inspect and retry.");
}
}

Project load_project(const fs::path& path, const std::string& profile) { return disk_bundle(path,{},profile).project; }

std::string save_structure_fingerprint(const Project& project) {
    if(!project.save_fingerprint.empty())return project.save_fingerprint;
    auto structure=[](const Project& p) {
        Json document=p.document.is_object()?p.document:Json::object();
        Json actions=Json::array();
        if(document.contains("input")&&document["input"].is_object()&&document["input"].contains("actions")&&document["input"]["actions"].is_object())
            for(const auto&[name,binding]:document["input"]["actions"].items())actions.push_back(name);
        for(auto key:{"title","ui","camera","game_camera","application","profiles","default_profile","scenarios","save_compatibility","input","scenes","entry_scene"})document.erase(key);
        Json entities=Json::array();
        for(const auto& e:p.entities)entities.push_back({{"id",e.id},{"name",e.name},{"shape",e.shape},{"body",e.body},{"role",e.role},
            {"position",e.position},{"size",e.size},{"color",e.color},{"mass",e.mass},{"visible",e.visible},{"asset",e.asset},
            {"animation",e.animation},{"rotation",e.rotation},{"flip_x",e.flip_x},{"animation_speed",e.animation_speed},
            {"visual_scale",e.visual_scale},{"collision_enabled",e.collision_enabled},{"controller",e.controller},{"parent",e.parent},{"parent_joint",e.parent_joint}});
        for(size_t i=0;i<p.entities.size();++i)if(!p.entities[i].physics2d.empty())entities[i]["physics2d"]=p.entities[i].physics2d;
        Json assets=Json::object();
        for(const auto&[id,asset]:p.assets) {
            Json inputs=Json::object();for(const auto&[path,bytes]:asset.inputs)inputs[path]=revision_for(bytes);
            assets[id]={{"type",asset.type},{"recipe",revision_for(asset.bytes.empty()?asset.document.dump():asset.bytes)},{"inputs",inputs}};
        }
        return Json{{"document",document},{"dimension",p.dimension},{"gravity",p.gravity},{"script",p.script_source},
            {"modules",p.modules},{"prefabs",p.prefabs},{"definitions",p.definitions},{"entities",entities},{"assets",assets},{"music",p.music},{"action_names",actions}};
    };
    Json scenes=Json::object();
    for(const auto&[id,scene]:project.scenes)scenes[id]=structure(*scene);
    if(!scenes.contains(project.scene_id))scenes[project.scene_id]=structure(project);
    return revision_for("save-structure-v1:"+scenes.dump());
}

Json save_compatibility_report(const Project& project,const Json& saved) {
    const auto identity=project.document.is_object()?project.document.value("project_id",project.title):project.title;
    const auto version=project.document.is_object()?project.document.value("save_version",1):1;
    const auto current=project.document.is_object()?project.document.value("save_compatibility",Json::object()):Json::object();
    Json result{{"ok",true},{"compatible",false},{"status","incompatible"},{"current_revision",project.revision},
        {"profile",project.profile},{"project_id",identity},{"game_save_version",version},
        {"structural_fingerprint",save_structure_fingerprint(project)},{"refresh_presentation",false},{"reasons",Json::array()}};
    auto reason=[&](const std::string& value){result["reasons"].push_back(value);};
    try {
        if(!saved.is_object()||saved.value("format_version",0)!=1){reason("Unsupported save format.");return result;}
        result["saved_revision"]=saved.value("source_revision",std::string());
        if(saved.value("project_id",std::string())!=identity){reason("Save belongs to another project.");return result;}
        if(saved.value("dimension",std::string())!=project.dimension){reason("Save dimension differs.");return result;}
        if(saved.value("source_revision",std::string())==project.revision&&saved.value("game_save_version",0)==version){
            result["compatible"]=true;result["status"]="exact";return result;
        }
        result["status"]="migration_required";
        if(saved.value("game_save_version",0)!=version){reason("Save schema version changed.");return result;}
        const auto previous=saved.value("save_compatibility",Json::object());
        if(!current.is_object()||!previous.is_object()||!current.value("allow_presentation_changes",false)||!previous.value("allow_presentation_changes",false)||
           current.value("id",std::string()).empty()||current.value("id",std::string())!=previous.value("id",std::string())) {
            reason("Both revisions must explicitly declare the same assessed presentation compatibility ID.");return result;
        }
        if(previous.value("structural_fingerprint",std::string())!=result["structural_fingerprint"].get<std::string>()) {
            reason("Gameplay structure or sources changed; explicit migration is required.");return result;
        }
        result["compatible"]=true;result["status"]="presentation_compatible";result["refresh_presentation"]=true;
        reason("Explicit declaration and structural fingerprint match; current authored UI and camera replace saved presentation.");
    }catch(const std::exception& error){result["status"]="incompatible";reason(std::string("Malformed compatibility metadata: ")+error.what());}
    return result;
}

Json project_summary(const Project& project) {
    Json entities = Json::array();
    for (const auto& e : project.entities) entities.push_back({{"id", e.id}, {"name", e.name}, {"shape", e.shape},
        {"body", e.body}, {"role", e.role}, {"position", e.position}, {"size", e.size}, {"color", e.color}, {"mass", e.mass}, {"visible", e.visible},
        {"asset", e.asset}, {"animation", e.animation}, {"rotation", e.rotation}, {"flip_x", e.flip_x}, {"animation_speed", e.animation_speed}, {"visual_scale",e.visual_scale},{"collision_enabled",e.collision_enabled},{"controller",e.controller},{"parent",e.parent},{"parent_joint",e.parent_joint}});
    for(size_t i=0;i<project.entities.size();++i)if(!project.entities[i].physics2d.empty())entities[i]["physics2d"]=project.entities[i].physics2d;
    Json assets = Json::object();
    for (const auto& [id, source] : project.assets) {
        Json inputs = Json::array();
        for (const auto& [path, bytes] : source.inputs) inputs.push_back({{"path", path}, {"bytes", bytes.size()}, {"revision", revision_for(bytes)}});
        assets[id] = {{"type", source.type}, {"path", project.document["assets"][id]["path"]}, {"revision", source.revision}, {"inputs", inputs}};
    }
    return {{"ok", true}, {"schema_version", project.document.value("schema_version",1)}, {"title", project.title}, {"dimension", project.dimension},
        {"revision", project.revision}, {"source_revision",project.source_revision},{"profile",project.profile},{"resolved_profile",project.resolved_profile},
        {"application",project.application},{"scenarios",project.scenarios},{"save_structure_fingerprint",save_structure_fingerprint(project)},
        {"save_compatibility",project.document.value("save_compatibility",Json::object())},{"resolved",project.document}, {"project_file", project.file.generic_string()},
        {"script", project.document["script"]}, {"entity_count", entities.size()}, {"entities", entities}, {"assets", assets}, {"music", project.music},{"scene",project.scene_id},{"modules",project.document.value("modules",Json::object())},{"scenes",project.document.value("scenes",Json::object())},{"prefabs",project.prefabs},{"ui",project.ui},{"definitions",project.definitions},{"source_count",project.sources.size()}};
}

Json capabilities() {
#if defined(LANGMAKE_IOS)
    constexpr const char* backend = "Metal";
    constexpr const char* platform = "iOS";
#elif defined(__ANDROID__)
    constexpr const char* backend = "OpenGL ES 3";
    constexpr const char* platform = "Android";
#elif defined(__APPLE__)
    constexpr const char* backend = "Metal";
    constexpr const char* platform = "macOS";
#else
    constexpr const char* backend = "Direct3D 11";
    constexpr const char* platform = "Windows";
#endif
    return {{"ok",true},{"engine","langmake"},{"version","0.5.0"},{"stage","Platform and optional Studio alpha"},{"schema_versions",{1,2}},
        {"dimensions",{"2d","3d"}},{"shapes",{"box","sphere","capsule"}},{"body_types",{"static","dynamic","none","kinematic","trigger"}},
        {"commands",{"help","capabilities","schema","script-api","check","validate","inspect","simulate","profile","patch","checkpoint","restore","preview","play","capture","control","save","load","new","cook","ui-check","save-check",
            "context","target","deploy","operation","verify","receipt","handoff","session","asset schema","asset list","asset inspect","asset build","asset render","asset export","asset replace"}},
        {"workflow",{{"guide","docs/26-game-workflow.md"},{"protocol","docs/25-workflow-implementation.md"},{"qualification","docs/27-workflow-qualification.md"},{"human_playtesting","Technical verification is distinct from physical input qualification and human gameplay acceptance"}}},
        {"ui_editor",{{"observation_revision",1},{"guide","docs/34-ui-editor-guide.md"},{"tool","scripts/ui-editor/ui-editor.cjs"},{"optional_studio",true},{"native_captures",true},{"runtime_editor_dependency",false}}},
        {"authoring",{{"external_agents",true},{"studio","Optional separate application; no engine model/provider dependency"},{"entry_point","project.json"}}},
        {"presentation_profiles",{{"selection","Explicit --profile or authored default_profile; never inferred from target"},{"overrides",{"input","ui","camera","game_camera","application"}},
            {"merge","Whole top-level field replacement, applied after scene overrides"},{"geometry","Gameplay geometry is never resized or replaced by a presentation profile"},{"max_profiles",32}}},
        {"save_compatibility",{{"default","Exact revision/save-version or explicit Luau migration"},{"opt_in","Matching explicit contract ID and structural fingerprint allow assessed presentation changes"},
            {"refresh","Cross-revision presentation reuse refreshes authored UI defaults and camera; all loads clear transient input before on_load"}}},
        {"cook",{{"targets",{"android","ios","macos","windows"}},{"encoding","validated-source-graph"},{"includes","Complete registered graph; excludes caches, saves, credentials and chat history"},{"asset_compilation","Native recipes are compiled by the player"}}},
        {"ios",{{"status","Native universal iPhone/iPad player and deployment implemented; simulator and physical-device coverage is scoped to recorded devices, scenarios and build revisions"},{"qualified",false},{"tools","scripts/ios/ios.py"},{"guide","docs/23-ios-agent-guide.md"},{"qualification","docs/27-workflow-qualification.md"},{"baseline_qualification","docs/24-ios-qualification.md"},{"device_families",{"iPhone","iPad"}},{"qualification_policy","No blanket device qualification; consult separate simulator, iPhone, iPad, physical-input and human-feedback evidence"},{"minimum_os","17.0"},{"graphics","Metal"},{"architectures",{{"device",{"arm64"}},{"simulator",{"arm64","x86_64"}}}},{"build_host","macOS with full Xcode"},{"orientations",{"landscape","portrait","sensor"}},{"external_agents",true},{"studio_required",false}}},
        {"android",{{"status","Player and game export implemented; see reference-device qualification"},{"tools","scripts/android/android.py"},{"guide","docs/18-android.md"},{"qualification","docs/20-android-qualification.md"},{"minimum_api",26},{"abis",{"arm64-v8a","x86_64"}},{"graphics","OpenGL ES 3"},{"orientations",{"landscape","portrait","sensor"}},{"player_variants",{"debug","release"}},{"standalone_games",true},{"hud_safe_area",true},{"script_budget_clock","Android thread CPU time; wall-clock fallback if unavailable"},{"touch","input.touch: virtual stick and paged named-action buttons; authored UI has input priority"}}},
        {"starter_templates",{"2d","3d","rockets-2d","rockets-3d","adventure","strategy","shared-2d","workflow-2d"}},
        {"project_operations",{"validate","inspect","patch","checkpoint","restore","new","check"}},
        {"patch_properties",{"name","position","size","color","mass","visible","shape","body","role","asset","animation","rotation","flip_x","animation_speed","visual_scale","collision_enabled","controller","physics2d"}},
        {"revision","Complete registered source graph; FNV-1a-64 change detection, not a security hash"},
        {"asset_types",{"sprite","mesh","music","model"}},
        {"assets",{{"sprites",{"ASCII palettes","drawing commands","PNG regions","frame animation"}},{"meshes",{"Manifold CSG primitives","part transforms and colors"}},
            {"models",{"Native joint/part/clip recipes","glTF/GLB triangle meshes, skins, textures and LINEAR animation","ozz sampling/blending","CPU skinning","OBJ posed export"}},{"music","Synthesized scores with filters, delay and reverb"}}},
        {"renderer",{{"library","bgfx"},{"backend",backend},{"platform",platform},{"materials",{"vertex color","base-color texture","simple lighting","unlit","transparency"}}}},
        {"physics",{{"2d","Box2D"},{"3d","Jolt"},{"body_rotation",{{"2d","opt-in physics2d.rotate"},{"3d",false}}},{"simulation","Fixed 1/60-second steps"},{"features",{"capsule character controllers","native raycasts, overlaps, sphere casts","contacts","independent collision participation","bounded convex 2D fixtures and angular motion (Windows tested)"}}}},
        {"material_grid",{{"kernel","classic-chemistry-v1"},{"guide","docs/38-material-grid-guide.md"},{"max_samples",262144},{"max_live_samples",1048576},{"presentation_qualified","Windows/D3D11"},{"features",{"native contiguous channels","active chunks","dirty surface regions","lazy bound state"}}}},
        {"navigation",{{"library","Recast/Detour 1.6.0"},{"modes",{"3D ground","top-down 2D"}},{"crowd_capacity",256},{"reference_agents",128},{"obstacle_updates","Explicit navigation_rebuild() from current registered static geometry"}}},
        {"ui",{{"library","RmlUi 6.3 + FreeType 2.14.3"},{"features",{"RML/RCSS screen-space HUD","text, panels, bars, images, buttons","keyboard/mouse/gamepad focus","inspectable values and semantic events"}},{"font","Lato Latin; Latin text coverage"}}},
        {"audio",{{"feature_revision",1},{"music_recipe_versions",{1,2}},{"authoring_guide","docs/30-music-and-sound-guide.md"},{"library","miniaudio"},{"optional",true},{"features",{"shared playback/offline mixer","WAV/FLAC recorded tracks","text composition via optional sfizz authoring","sample-aligned adaptive layers","parametric EQ/compressor/peak limiter","master/music/sfx/ui and custom buses","lowpass/highpass","delay","reverb","smoothed gain/pan/mute","bounded voices and command queue"}}}},
        {"preview",{{"source_watch",true},{"reload","Complete source graph; restart on valid changes, keep last good world on load failure"},{"live_state",".langmake/live.json"},{"control_api_version",2},{"compact_receipts",true},
            {"live_controls",{"pause","resume","reset","snapshot","step","capture","save","load"}},{"live_queries","--ids --role --visible --fields --offset --limit; --check-source explicitly reads authored sources"},
            {"capture",{{"format","PNG"},{"timing",{"legacy --frames","isolated fixed-tick --steps --input --ticks --output-dir"}},{"metadata","Tick, scene, source/asset revisions, camera, viewport, scenario and state"}}}}},
        {"play",{{"overlay",false},{"source_watch",false},{"developer_shortcuts",false}}},
        {"scripts",{{"language","Luau"},{"entry","Return optional start(), update(dt), on_load(), on_recover(), migrate(saved,from,to) hooks"},{"api",script_api()["functions"]},{"modules","Registered require(ID), sandboxed/cache-once; cycles diagnosed"},{"checking","check command and script-api generated declarations"}}},
        {"world",{{"active_scenes",1},{"scene_registry_overrides",{"assets","modules","prefabs","definitions"}},{"features",{"transactional scene changes","bounded prefab spawning/destruction","JSON gameplay state","versioned project-scoped saves and migration hooks","fixed-tick gameplay cameras"}},{"starter_systems",{"health/combat","inventory/equipment","dialogue/quests","timers/state machines","cinematics","group orders/tasks"}}}},
        {"limits",{{"max_entities",4096},{"max_assets",256},{"max_source_files",2048},{"max_profiles",32},{"max_file_bytes",4*1024*1024},{"max_binary_file_bytes",32*1024*1024},{"max_source_bytes",256*1024*1024},{"max_v1_source_bytes",32*1024*1024},
            {"max_compiled_visual_bytes",256*1024*1024},{"max_project_music_notes",1024*1024},{"max_script_source_bytes",1024*1024},{"script_memory_bytes",16*1024*1024},{"script_call_budget_ms",50},{"players",1},
            {"model_joints",128},{"model_vertices",100000},{"model_clips",64},{"recorded_music_tracks",16},{"recorded_music_asset_bytes",96*1024*1024},{"recorded_music_project_bytes",192*1024*1024},{"audio_buses",8},{"effects_per_bus",4},{"delay_seconds",2},{"audio_tail_seconds",10},{"receipt_queue_bytes",16*1024*1024},{"receipt_retention_seconds",600}}},
        {"limitations",{"Windows/D3D11, Android/GLES3 and iOS/Metal coverage is limited to the devices, scenarios and build revisions in the current qualification record; do not infer another device or feature from a platform label; consoles and XR remain future work","One active scene; ground navigation; no streaming or multiplayer","No FBX, retargeting, morph targets, root motion or advanced/PBR materials","Recorded WAV/FLAC playback and text-authored sampled instruments are supported; no neural music/vocal generation, arbitrary sampler plugins, or streaming audio","Legacy and 3D colliders remain axis aligned; opt-in physics2d supports convex fixtures and Z rotation; visual_scale never changes collision","Save reconstructs systems; it does not serialize a physics solver or Luau VM","Source restore rolls back ordinary I/O failures but is not a crash-atomic multi-file transaction","External editors do not participate in the engine mutation lock"}}};
}

Json project_schema() {
    Json entity = {{"type", "object"}, {"additionalProperties", false}, {"required", {"id", "position", "size"}},
        {"properties", {{"id", {{"type", "string"}, {"pattern", "^[A-Za-z0-9_-]{1,64}$"}}},
            {"name", {{"type", "string"}, {"minLength", 1}, {"maxLength", 160}}},
            {"shape", {{"enum", {"box", "sphere", "capsule"}}, {"default", "box"}}},
            {"body", {{"enum", {"static", "dynamic", "none", "kinematic", "trigger"}}, {"default", "static"}}},
            {"role", {{"type", "string"}, {"pattern","^[A-Za-z0-9_-]{1,64}$"}, {"default", "decoration"}}},
            {"position", number_array_schema(3, -100000, 100000)}, {"size", number_array_schema(3, 0.01, 10000)},
            {"color", number_array_schema(4, 0, 1)}, {"mass", {{"type", "number"}, {"minimum", 0.001}, {"maximum", 100000}, {"default", 1}}},
            {"visible", {{"type", "boolean"}, {"default", true}}}}}};
    entity["properties"]["asset"] = {{"type", "string"}, {"pattern", "^[A-Za-z0-9_-]{0,64}$"}, {"description", "Registered sprite/mesh asset; empty uses primitive visual. Default tint is white when an asset is selected."}};
    entity["properties"]["animation"] = {{"type", "string"}, {"pattern", "^[A-Za-z0-9_-]{0,64}$"}, {"description", "Sprite animation ID; empty selects the recipe default."}};
    entity["properties"]["rotation"] = number_array_schema(3, -360000, 360000);
    entity["properties"]["flip_x"] = {{"type", "boolean"}, {"default", false}};
    entity["properties"]["animation_speed"] = {{"type", "number"}, {"minimum", 0.001}, {"maximum", 1000}, {"default", 1}};
    entity["properties"]["animation"]={{"type","string"},{"maxLength",128},{"description","Registered clip name; models permit printable names, sprites use ASCII IDs."}};
    entity["properties"]["parent"]={{"type","string"},{"maxLength",64}};
    entity["properties"]["parent_joint"]={{"type","string"},{"maxLength",128}};
    entity["properties"]["physics2d"]={{"type","object"},{"additionalProperties",false},{"properties",{
        {"rotate",{{"type","boolean"}}},{"friction",{{"type","number"},{"minimum",0},{"maximum",1}}},
        {"restitution",{{"type","number"},{"minimum",0},{"maximum",1}}},{"angular_damping",{{"type","number"},{"minimum",0},{"maximum",20}}},
        {"polygons",{{"type","array"},{"minItems",1},{"maxItems",4},{"items",{{"type","array"},{"minItems",3},{"maxItems",8},{"items",number_array_schema(2,-.5,.5)}}}}}}}};
    entity["properties"]["visual_scale"]=number_array_schema(3,.001,1000);
    entity["properties"]["collision_enabled"]={{"type","boolean"},{"default",true}};
    entity["properties"]["controller"]={{"enum",{"","character"}}};
    Json registration = {{"type", "object"}, {"additionalProperties", false}, {"required", {"type", "path"}},
        {"properties", {{"type", {{"enum", {"sprite", "mesh", "music", "model"}}}}, {"path", {{"type", "string"}, {"minLength", 1}, {"maxLength", 240},
            {"description", "Project-relative .json recipe path without traversal or reserved .langmake components."}}}}}};
    Json schema = {{"$schema", "https://json-schema.org/draft/2020-12/schema"}, {"title", "Langmake 0.4 project"},
        {"type", "object"}, {"additionalProperties", false}, {"required", {"schema_version", "dimension", "title", "script", "entities"}},
        {"properties", {{"schema_version", {{"type", "integer"}, {"enum", {1,2}}}}, {"dimension", {{"enum", {"2d", "3d"}}}},
            {"title", {{"type", "string"}, {"minLength", 1}, {"maxLength", 160}}},
            {"assets", {{"type", "object"}, {"maxProperties", max_assets}, {"propertyNames", {{"pattern", "^[A-Za-z0-9_-]{1,64}$"}}}, {"additionalProperties", registration}}},
            {"music", {{"type", "string"}, {"pattern", "^[A-Za-z0-9_-]{1,64}$"}, {"description", "Registered music asset ID for background playback."}}},
            {"script", {{"type", "string"}, {"minLength", 1}, {"maxLength", 240}, {"description", "Relative .luau file, '/' separators, no parent traversal; validated against the project root at load time"}}},
            {"gravity", number_array_schema(3, -1000, 1000)},
            {"camera", {{"type", "object"}, {"additionalProperties", false}, {"required", {"position", "target"}},
                {"properties", {{"position", number_array_schema(3, -100000, 100000)}, {"target", number_array_schema(3, -100000, 100000)}}}}},
            {"entities", {{"type", "array"}, {"minItems", 0}, {"maxItems", max_entities}, {"items", entity}}}}},
        {"$comment", "The validate command also checks unique IDs and recipe paths, asset and sprite-animation references, opaque mesh entity tint, project-scoped source dependencies and byte limits, asset compilation, at most one player with a dynamic body, control characters, distinct camera position/target, and equal x/y/z sizes for collidable spheres (body other than 'none'). Sprite and mesh visuals do not automatically change collision; optional physics2d supplies bounded convex fixtures and Z rotation for unparented 2D bodies. Asset schemas are available through asset schema --type sprite|mesh|music."}};
    auto& props=schema["properties"];
    for(auto name:{"modules","prefabs","scenes","definitions","scenarios"})props[name]={{"type","object"},{"maxProperties",256},{"propertyNames",{{"pattern","^[A-Za-z0-9_-]{1,64}$"}}},{"additionalProperties",{{"type","string"},{"maxLength",240}}}};
    for(auto name:{"ui","navigation","audio","state","input","game_camera"})props[name]={{"type","object"},{"description","See docs/13-runtime-guide.md; validate checks subsystem semantics."}};
    Json touch_button={{"type","object"},{"additionalProperties",false},{"required",{"action"}},
        {"properties",{{"action",{{"type","string"}}},{"label",{{"type","string"},{"minLength",1},{"maxLength",24}}}}}};
    Json touch_stick={{"type","object"},{"additionalProperties",false},{"properties",{
        {"enabled",{{"type","boolean"}}},{"x_action",{{"type","string"}}},{"y_action",{{"type","string"}}}}}};
    Json touch_schema={{"type","object"},{"additionalProperties",false},{"properties",{
        {"enabled",{{"type","boolean"}}},{"stick",touch_stick},
        {"buttons",{{"type","array"},{"maxItems",64},{"items",touch_button}}}}}};
    props["input"]={{"type","object"},{"additionalProperties",false},{"properties",{
        {"actions",{{"type","object"},{"maxProperties",64}}},{"touch",touch_schema}}}};
    props["ui"]["properties"]["expected_characters"]={{"type","string"},{"maxLength",16384},{"description","UTF-8 text for dynamic glyph coverage; loader applies a 16384-byte bound."}};
    Json app_fields={{"id",{{"type","string"},{"minLength",1},{"maxLength",255}}},
        {"icon",{{"type","string"},{"minLength",1},{"maxLength",240},{"description","Project-relative PNG; native tools validate platform image requirements."}}},
        {"orientation",{{"enum",{"portrait","landscape","sensor"}}}}};
    Json app_platform={{"type","object"},{"additionalProperties",false},{"properties",app_fields}};
    app_fields["label"]={{"type","string"},{"minLength",1},{"maxLength",160}};
    app_fields["version"]={{"type","string"},{"pattern","^[0-9]+(\\.[0-9]+)*$"},{"maxLength",64},{"default","1.0"}};
    app_fields["build"]={{"type","integer"},{"minimum",1},{"maximum",2100000000},{"default",1}};
    app_fields["platforms"]={{"type","object"},{"additionalProperties",false},{"properties",{{"android",app_platform},{"ios",app_platform},{"macos",app_platform},{"windows",app_platform}}}};
    props["application"]={{"type","object"},{"additionalProperties",false},{"properties",app_fields}};
    props["save_compatibility"]={{"type","object"},{"additionalProperties",false},{"required",{"id","allow_presentation_changes"}},
        {"properties",{{"id",{{"type","string"},{"pattern","^[A-Za-z0-9_-]{1,64}$"}}},{"allow_presentation_changes",{{"type","boolean"}}}}}};
    Json profile_properties=Json::object();for(auto key:{"input","ui","camera","game_camera","application"})profile_properties[key]=props[key];
    props["profiles"]={{"type","object"},{"maxProperties",32},{"propertyNames",{{"pattern","^[A-Za-z0-9_-]{1,64}$"}}},
        {"additionalProperties",{{"type","object"},{"additionalProperties",false},{"properties",profile_properties}}}};
    props["default_profile"]={{"type","string"},{"pattern","^[A-Za-z0-9_-]{1,64}$"}};
    props["project_id"]={{"type","string"},{"pattern","^[A-Za-z0-9_-]{1,64}$"}};
    props["save_version"]={{"type","integer"},{"minimum",1},{"maximum",1000000}};
    props["seed"]={{"type","integer"},{"minimum",0},{"maximum",9007199254740991ull}};props["entry_scene"]={{"type","string"}};
    schema["allOf"]=Json::array({{{"if",{{"properties",{{"schema_version",{{"const",1}}}}}}},{"then",{{"properties",{{"entities",{{"minItems",1}}},{"modules",false},{"prefabs",false},{"scenes",false},{"definitions",false},
        {"profiles",false},{"default_profile",false},{"application",false},{"scenarios",false},{"save_compatibility",false}}}}}}});
    schema["$comment"]="validate also checks source containment, registry references, semantic limits and subsystem initialization. Version 1 retains its original player/role/mesh constraints. Visual assets do not alter physics colliders. Use asset schema for recipe contracts and script-api for bindings.";
    return schema;
}

Json patch_project(const fs::path& path, const Json& patch, const std::string& profile) {
    try {
        const auto initial = load_project(path,profile);
        MutationLock lock(initial.root);
        auto project = load_project(path,profile);
        Json errors = Json::array();
        if (!object_keys(patch, {"expected_revision", "changes", "scene"}, "", errors)) return {{"ok", false}, {"diagnostics", errors}};
        required(patch, {"expected_revision", "changes"}, "", errors);
        if (patch.contains("expected_revision") && !patch["expected_revision"].is_string())
            issue(errors, "type", "/expected_revision", "Expected a revision string from inspect or a prior mutation.");
        if (patch.contains("changes") && (!patch["changes"].is_array() || patch["changes"].empty() || patch["changes"].size() > 10000))
            issue(errors, "type", "/changes", "Expected between 1 and 10000 changes.");
        if (!errors.empty()) return {{"ok", false}, {"diagnostics", errors}};
        if (patch["expected_revision"] != project.revision) {
            auto result = failure("revision_conflict", "/expected_revision", "Project sources changed; inspect again before patching.");
            result["current_revision"] = project.revision;
            return result;
        }
        std::set<std::pair<std::string, std::string>> changed;
        auto manifest=parse_document(project.sources.at(project.file.filename().generic_string()));
        auto scene=patch.value("scene",project.scene_id);
        auto target_relative=manifest.value("scenes",Json::object()).contains(scene)?manifest["scenes"][scene].get<std::string>():project.file.filename().generic_string();
        if(patch.contains("scene")&&!manifest.value("scenes",Json::object()).contains(scene))return failure("scene_reference","/scene","Unknown scene ID.");
        auto patch_document=parse_document(project.sources.at(target_relative));
        if(!patch_document.contains("entities"))return failure("entities","/entities","This source has no authored entity array.");
        for (std::size_t i = 0; i < patch["changes"].size(); ++i) {
            const auto& change = patch["changes"][i];
            const auto location = "/changes/" + std::to_string(i);
            if (!object_keys(change, {"id", "property", "value"}, location, errors)) continue;
            required(change, {"id", "property", "value"}, location, errors);
            if (!change.contains("id") || !change["id"].is_string() || !change.contains("property") || !change["property"].is_string() || !change.contains("value")) {
                issue(errors, "type", location, "Each change needs string id/property and a value.");
                continue;
            }
            const auto id = change["id"].get<std::string>(), property = change["property"].get<std::string>();
            static const std::set<std::string> properties{"name", "position", "size", "color", "mass", "visible", "shape", "body", "role", "asset", "animation", "rotation", "flip_x", "animation_speed", "visual_scale", "collision_enabled", "controller", "parent", "parent_joint", "physics2d"};
            if (!properties.contains(property)) { issue(errors, "property", location + "/property", "Property cannot be patched by this API."); continue; }
            if (!changed.emplace(id, property).second) { issue(errors, "duplicate_change", location, "The same entity property occurs twice in this patch."); continue; }
            auto& entities = patch_document["entities"];
            const auto entity = std::find_if(entities.begin(), entities.end(), [&id](const Json& e) { return e["id"] == id; });
            if (entity == entities.end()) issue(errors, "entity_not_found", location + "/id", "No entity has ID '" + id + "'.");
            else (*entity)[property] = change["value"];
        }
        if (!errors.empty()) return {{"ok", false}, {"diagnostics", errors}};
        const auto bytes = patch_document.dump(2) + "\n";
        const auto candidate = disk_bundle(path, {{target_relative, bytes}},profile);
        // A direct editor may have changed any source while validation ran.
        if (load_project(path,profile).revision != project.revision) return failure("revision_conflict", "/expected_revision", "Project changed during patch validation.");
        const auto backup = scoped(project.root, ".langmake/project.before-patch.json");
        atomic_write(project.root, scoped(project.root,target_relative), bytes, backup);
        return {{"ok", true}, {"previous_revision", project.revision}, {"revision", candidate.project.revision},
            {"changes_applied", patch["changes"].size()}, {"backup", backup.generic_string()}, {"diagnostics", Json::array()}};
    } catch (const std::exception& error) { return failure("patch_failed", "", error.what()); }
}

namespace {
fs::path stored_source(const std::string& relative, const std::string& manifest, const std::string& script) {
    if (relative == manifest) return "project.json";
    if (relative == script) return "game.luau";
    return fs::path("files") / relative;
}
void write_snapshot(const fs::path& root, const fs::path& destination, const SourceFiles& files,
                    const std::string& manifest, const std::string& script, const Json& revision) {
    Json hashes = Json::object();
    const auto base = destination.lexically_relative(root);
    for (const auto& [relative, bytes] : files) {
        const auto output = scoped(root, base / stored_source(relative, manifest, script));
        fs::create_directories(output.parent_path());
        write_file(output, bytes);
        hashes[relative] = revision_for(bytes);
    }
    write_file(scoped(root, base / "checkpoint.json"), Json({{"format_version", 2}, {"manifest", manifest}, {"script", script},
        {"revision", revision}, {"files", hashes}}).dump(2) + "\n");
}
SourceBundle read_checkpoint(const Project& location, const fs::path& source) {
    const auto& root = location.root;
    const auto base = source.lexically_relative(root);
    const auto manifest = read_file(scoped(root, base / "project.json"));
    const auto metadata = parse_document(read_file(scoped(root, base / "checkpoint.json")));
    const auto document = parse_document(manifest);
    ensure_valid(document);
    if (!metadata.is_object() || !metadata.contains("format_version") || !metadata["format_version"].is_number_integer())
        throw std::runtime_error("Invalid checkpoint metadata.");
    const auto manifest_name = location.file.filename().generic_string();
    const auto script_name = document["script"].get<std::string>();
    if (!metadata.contains("script") || metadata["script"] != script_name) throw std::runtime_error("Checkpoint script metadata does not match its manifest.");
    if (metadata["format_version"] == 1) {
        // Legacy snapshots never contained assets: do not resolve missing saved inputs from the live tree.
        if (document.contains("assets") && !document["assets"].empty()) throw std::runtime_error("Legacy checkpoints cannot contain registered assets.");
        const auto script = read_file(scoped(root, base / "game.luau"));
        if (metadata.value("manifest_hash", "") != revision_for(manifest) || metadata.value("script_hash", "") != revision_for(script) ||
            metadata.value("revision", "") != combined_revision(manifest, script)) throw std::runtime_error("Legacy checkpoint content does not match its metadata.");
        return read_bundle(root, location.file, [&](const std::string& relative) {
            if (relative == manifest_name) return manifest;
            if (relative == script_name) return script;
            throw std::runtime_error("Unexpected source in legacy checkpoint.");
        });
    }
    if (metadata["format_version"] != 2 || metadata.value("manifest", "") != manifest_name || !metadata.contains("files") ||
        !metadata["files"].is_object() || metadata["files"].size() > 2048)
        throw std::runtime_error("Unsupported or malformed checkpoint source list.");
    const auto& hashes = metadata["files"];
    auto bundle = read_bundle(root, location.file, [&](const std::string& relative) {
        if (!hashes.contains(relative) || !hashes[relative].is_string()) throw std::runtime_error("Checkpoint is missing source metadata: " + relative);
        const auto bytes = relative == manifest_name ? manifest : read_file(scoped(root, base / stored_source(relative, manifest_name, script_name)));
        if (hashes[relative] != revision_for(bytes)) throw std::runtime_error("Checkpoint source hash does not match: " + relative);
        return bytes;
    });
    if (bundle.files.size() != hashes.size() || metadata.value("revision", "") != bundle.project.revision)
        throw std::runtime_error("Checkpoint dependency graph or revision does not match its metadata.");
    return bundle;
}
}

Json replace_asset(const fs::path& path, const std::string& id, const std::string& expected_revision, const Json& document, const std::string& profile) {
    try {
        const auto initial = load_project(path,profile);
        MutationLock lock(initial.root);
        const auto current = disk_bundle(path,{},profile);
        if (expected_revision != current.project.revision) {
            auto result = failure("revision_conflict", "/expected_revision", "Project sources changed; inspect again before replacing an asset.");
            result["current_revision"] = current.project.revision;
            return result;
        }
        if (!current.project.assets.contains(id)) return failure("asset_not_found", "/id", "No asset has ID '" + id + "'.");
        if (!document.is_object()) return failure("type", "/document", "Expected a complete asset recipe object.");
        const auto relative = current.project.document["assets"][id]["path"].get<std::string>();
        const auto bytes = document.dump(2) + "\n";
        const auto candidate = disk_bundle(path, {{relative, bytes}},profile);
        const auto& asset = candidate.project.assets.at(id);
        // A recipe can be valid by itself while exceeding the project's shared
        // compiled budgets or breaking a cross-asset runtime constraint.
        (void)compile_assets(candidate.project);
        check_unchanged(path, current.project.revision,profile);
        const auto backup = scoped(current.project.root, fs::path(".langmake/assets-before-patch") / (id + ".json"));
        atomic_write(current.project.root, asset.path, bytes, backup);
        return {{"ok", true}, {"id", id}, {"previous_revision", current.project.revision}, {"revision", candidate.project.revision},
            {"asset_revision", asset.revision}, {"backup", backup.generic_string()}, {"diagnostics", Json::array()}};
    } catch (const std::exception& error) { return failure("asset_patch_failed", "/document", error.what()); }
}

void copy_project_sources(const Project& project, const fs::path& destination) {
    const auto target = fs::weakly_canonical(fs::absolute(destination));
    if (fs::exists(target)) throw std::runtime_error("Destination already exists; refusing to overwrite it.");
    MutationLock lock(project.root);
    const auto bundle = disk_bundle(project.file,{},project.profile);
    if (bundle.project.revision != project.revision) throw std::runtime_error("Project changed before its sources could be copied.");
    const auto pending = target.parent_path() / (target.filename().string() + ".pending-" + unique_suffix());
    if (!contained(target.parent_path(), pending) || pending == target.parent_path()) throw std::runtime_error("Invalid copy staging path.");
    fs::create_directories(pending);
    try {
        for (const auto& [relative, bytes] : bundle.files) {
            auto output_relative = relative == project.file.filename().generic_string() ? std::string("project.json") : relative;
            if (relative != project.file.filename().generic_string() && output_relative == "project.json")
                throw std::runtime_error("Cannot copy this project's manifest to project.json because an asset occupies that path.");
            const auto output = scoped(pending, output_relative);
            fs::create_directories(output.parent_path());
            write_file(output, bytes);
        }
        check_unchanged(project.file, project.revision,project.profile);
        fs::rename(pending, target);
    } catch (...) { std::error_code ignored; fs::remove_all(pending, ignored); throw; }
}

Json checkpoint_project(const fs::path& path, const std::string& name) {
    fs::path pending;
    try {
        const auto initial = load_project(path);
        MutationLock lock(initial.root);
        const auto bundle = disk_bundle(path);
        const auto& project = bundle.project;
        const auto destination = checkpoint_path(project, name);
        if (fs::exists(destination)) return failure("checkpoint_exists", "/name", "A checkpoint with this name already exists; choose another name.");
        pending = scoped(project.root, fs::path(".langmake/checkpoints") / ("pending-" + unique_suffix()));
        fs::create_directories(pending);
        auto authored=parse_document(bundle.files.at(project.file.filename().generic_string()));
        write_snapshot(project.root, pending, bundle.files, project.file.filename().generic_string(), authored["script"].get<std::string>(), project.revision);
        check_unchanged(path, project.revision);
        fs::rename(pending, destination);
        pending.clear();
        return {{"ok", true}, {"name", name}, {"revision", project.revision}, {"path", destination.generic_string()},
            {"source_files", bundle.files.size()}, {"diagnostics", Json::array()}};
    } catch (const std::exception& error) {
        if (!pending.empty()) { std::error_code ignored; fs::remove_all(pending, ignored); }
        return failure("checkpoint_failed", "/name", error.what());
    }
}

Json restore_project(const fs::path& path, const std::string& name) {
    try {
        // Recovery does not require the live manifest, asset JSON or input files to load.
        const auto [root, file] = project_location(path);
        Project location;
        location.root = root;
        location.file = file;
        MutationLock lock(root);
        const auto bundle = read_checkpoint(location, checkpoint_path(location, name));
        SourceBundle current;
        bool current_valid = false;
        try { current = disk_bundle(path); current_valid = true; } catch (const std::exception&) {}
        struct Target { fs::path path; std::string relative, next, old; bool existed; };
        std::vector<Target> targets;
        SourceFiles previous;
        std::size_t backup_bytes = 0;
        for (const auto& [relative, bytes] : bundle.files) {
            const auto target = scoped(root, relative);
            const bool exists = fs::exists(target);
            const auto old = exists ? read_file(target) : std::string{};
            if (old.size() > 256*1024*1024 - backup_bytes) throw std::runtime_error("Existing restore targets exceed the 256 MiB backup limit.");
            backup_bytes += old.size();
            if (exists) previous[relative] = old;
            targets.push_back({target, relative, bytes, old, exists});
        }
        std::stable_sort(targets.begin(), targets.end(), [&](const Target& a, const Target& b) {
            return (a.path == file ? 1 : 0) < (b.path == file ? 1 : 0);
        });
        const auto backup = scoped(root, ".langmake/before-restore");
        fs::create_directories(backup);
        write_snapshot(root, backup, current_valid ? current.files : previous, file.filename().generic_string(),
            parse_document((current_valid ? current.files : bundle.files).at(file.filename().generic_string()))["script"].get<std::string>(),
            current_valid ? Json(current.project.revision) : Json(nullptr));
        for (const auto& target : targets)
            if (fs::exists(target.path) != target.existed || (target.existed && read_file(target.path) != target.old))
                throw std::runtime_error("A source changed during restore preparation.");
        if (current_valid) check_unchanged(path, current.project.revision);
        std::size_t applied = 0;
        try {
            for (const auto& target : targets) { atomic_write(root, target.path, target.next); ++applied; }
        } catch (const std::exception& original) {
            const std::string message = original.what();
            std::string rollback_error;
            while (applied) {
                const auto& target = targets[--applied];
                try {
                    if (target.existed) atomic_write(root, target.path, target.old);
                    else fs::remove(target.path);
                } catch (const std::exception& error) { rollback_error += " " + std::string(error.what()); }
            }
            throw std::runtime_error(message + (rollback_error.empty() ? " Applied writes were rolled back." : " Rollback failed; recover from " + backup.generic_string() + ":" + rollback_error));
        }
        return {{"ok", true}, {"name", name}, {"previous_revision", current_valid ? Json(current.project.revision) : Json(nullptr)},
            {"revision", bundle.project.revision}, {"source_files", bundle.files.size()}, {"backup", backup.generic_string()}, {"diagnostics", Json::array()}};
    } catch (const std::exception& error) { return failure("restore_failed", "/name", error.what()); }
}
}
