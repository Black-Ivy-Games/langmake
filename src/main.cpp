#include "langmake/project.hpp"
#include "langmake/cook.hpp"
#include "langmake/world.hpp"
#include "langmake/preview.hpp"
#include "langmake/io.hpp"
#include "langmake/timeline.hpp"
#include "langmake/model.hpp"
#include "langmake/audio_mixer.hpp"
#include "langmake/ui.hpp"
#include "langmake/script_api.hpp"
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_filesystem.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <thread>

namespace {
using lm::Json;
struct Arguments {
    std::string command = "help";
    std::map<std::string, std::string> options;
    explicit Arguments(int argc, char** argv) {
        if (argc > 1) command = argv[1];
        int start = 2;
        if (command == "asset") {
            if (argc < 3 || std::string(argv[2]).starts_with("--")) throw std::runtime_error("asset requires schema, list, inspect, build, render, export, or replace");
            command += "-" + std::string(argv[2]); start = 3;
        }
        for (int i = start; i < argc; ++i) {
            std::string key = argv[i];
            if (!key.starts_with("--")) throw std::runtime_error("Expected --option, found: " + key);
            if (options.contains(key)) throw std::runtime_error("Duplicate option: " + key);
            const bool is_flag = key == "--live" || key == "--no-audio" || key == "--include-state" || key == "--check-source" || key == "--no-overlay" || key == "--summary";
            const bool has_value = i + 1 < argc && !std::string(argv[i + 1]).starts_with("--");
            if (is_flag) {
                if (has_value) throw std::runtime_error("Flag does not accept a value: " + key);
                options[key] = "true";
            } else {
                if (!has_value || std::string(argv[i + 1]).empty())
                    throw std::runtime_error("Option requires a nonempty value: " + key);
                options[key] = argv[++i];
            }
        }
    }
    std::string get(const std::string& key, const std::string& fallback = "") const {
        auto it = options.find(key); return it == options.end() ? fallback : it->second;
    }
    int number(const std::string& key, int fallback, int maximum) const {
        auto value = get(key); if (value.empty()) return fallback;
        size_t consumed = 0; int result = std::stoi(value, &consumed);
        if (consumed != value.size() || result < 0 || result > maximum) throw std::runtime_error("Invalid bounded integer for " + key);
        return result;
    }
    void allow(std::initializer_list<std::string> keys) const {
        for (const auto& [key, value] : options)
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) throw std::runtime_error("Unknown option: " + key);
    }
    float real(const std::string& key,float fallback,float minimum,float maximum) const {
        const auto value=get(key);if(value.empty())return fallback;
        std::size_t parsed=0;const auto number=std::stof(value,&parsed);
        if(parsed!=value.size()||!std::isfinite(number)||number<minimum||number>maximum)throw std::runtime_error("Invalid bounded number for "+key);
        return number;
    }
};
std::array<int,4> safe_rectangle(const Arguments& args,int width,int height) {
    std::array<int,4> result{};if(args.get("--safe-area").empty())return result;
    std::istringstream stream(args.get("--safe-area"));std::string part;int index=0;
    while(std::getline(stream,part,',')){std::size_t count=0;const int value=std::stoi(part,&count);if(index>=4||count!=part.size()||value<0)throw std::runtime_error("safe-area requires x,y,width,height in physical pixels");result[index++]=value;}
    if(index!=4||result[2]<=0||result[3]<=0||result[0]+int64_t(result[2])>width||result[1]+int64_t(result[3])>height)throw std::runtime_error("safe-area must be a positive rectangle inside the viewport");
    return result;
}
int workflow_command(int argc,char** argv) {
    const char* base=SDL_GetBasePath();if(!base)throw std::runtime_error(std::string("Cannot resolve runtime directory: ")+SDL_GetError());
    auto executable=std::filesystem::path(base);
    if(executable.filename().empty())executable=executable.parent_path();
    auto script=executable/"scripts/workflow/workflow.py";
    if(!std::filesystem::is_regular_file(script))script=executable.parent_path()/"scripts/workflow/workflow.py";
    if(!std::filesystem::is_regular_file(script))throw std::runtime_error("Authoring workflow tools are missing; use a current complete source or runtime package.");
    const char* configured=std::getenv("LANGMAKE_PYTHON");
#ifdef _WIN32
    const std::string python=configured?configured:"python.exe";
#else
    const std::string python=configured?configured:"python3";
#endif
    std::vector<std::string> values{python,script.string(),"--engine",(executable/std::filesystem::path(argv[0]).filename()).string()};
    for(int i=1;i<argc;++i)values.emplace_back(argv[i]);
    std::vector<const char*> arguments;for(const auto& value:values)arguments.push_back(value.c_str());arguments.push_back(nullptr);
    auto* process=SDL_CreateProcess(arguments.data(),false);
    if(!process)throw std::runtime_error(std::string("Cannot start optional Python authoring tools. Install Python 3.9+ or set LANGMAKE_PYTHON. ")+SDL_GetError());
    int code=1;const bool waited=SDL_WaitProcess(process,true,&code);SDL_DestroyProcess(process);
    if(!waited)throw std::runtime_error("Cannot wait for authoring command");return code;
}
Json read_json(const std::filesystem::path& path) {
    return lm::read_json_shared(path,16*1024*1024);
}
Json observation_query(const Arguments& args) {
    Json query{{"offset",args.number("--offset",0,4096)},{"limit",args.number("--limit",128,4096)}};
    for(auto [option,key]:{std::pair{"--ids","ids"},std::pair{"--fields","fields"}}) if(!args.get(option).empty()) {
        query[key]=Json::array();std::istringstream stream(args.get(option));std::string part;while(std::getline(stream,part,','))query[key].push_back(part);
    }
    if(!args.get("--role").empty())query["role"]=args.get("--role");
    if(!args.get("--visible").empty()){if(args.get("--visible")!="true"&&args.get("--visible")!="false")throw std::runtime_error("visible must be true or false");query["visible"]=args.get("--visible")=="true";}
    return query;
}
std::filesystem::path project_directory(const std::filesystem::path& path) {
    auto absolute = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
    return std::filesystem::is_directory(absolute) ? absolute : absolute.parent_path();
}
int event_tick(const Json& event, const char* field) {
    if (!event.is_object() || !event.contains(field) || !event[field].is_number_integer())
        throw std::runtime_error(std::string("Input event '") + field + "' must be an integer");
    const auto& value = event[field];
    // Check the JSON integer before narrowing: uint64 values must not wrap.
    if (value < 0 || value > 36000)
        throw std::runtime_error(std::string("Input event '") + field + "' must be between 0 and 36000");
    return value.get<int>();
}
Json exception_result(const std::exception& error) {
    auto details = Json::parse(error.what(), nullptr, false);
    if (details.is_object() && details.contains("diagnostics") && details["diagnostics"].is_array() && !details["diagnostics"].empty()) {
        details["ok"] = false;
        return details;
    }
    if (details.is_object() && details.contains("code") && details["code"].is_string() &&
        details.contains("message") && details["message"].is_string())
        return {{"ok", false}, {"diagnostics", Json::array({details})}};
    return {{"ok", false}, {"diagnostics", Json::array({{{"code", "operation_failed"}, {"message", error.what()}}})}};
}
lm::Input read_input(const Json& value) {
    return lm::parse_input(value);
}
Json help() {
    return {{"ok", true}, {"engine", "Langmake"}, {"version", "0.5.0"},
        {"usage", "langmake COMMAND --project PATH [options]"},
        {"commands", {
            {"context", "Report current executable/tool provenance, game metadata and per-game target setup"},
            {"target", "Explicit game-local target add/list/check/remove; no machine defaults are inherited"},
            {"deploy", "Run a recorded platform build/install/launch operation for a configured game target"},
            {"operation", "Inspect or resume a recorded deployment operation"},
            {"verify", "Run a registered game scenario with compact results and preserved evidence"},
            {"receipt", "Dispatch one native command and validate its bounded, session-bound receipt"},
            {"handoff", "Generate a portable per-game development handoff without local credentials"},
            {"session", "Human play sessions: show|release|protect|launch --project PATH; protect/launch require --target NAME; launch [--development] [--fresh] for debug verification targets"},
            {"ui-check", "Headless UI/layout/glyph diagnostics: --width --height --density --safe-area x,y,w,h"},
            {"save-check", "Report assessed save compatibility: --project PATH --file SAVE.json [--profile NAME]"},
            {"capabilities", "List implemented systems and honest platform status"},
            {"cook", "Validate and bundle the full source graph: --project PATH --target android|ios|windows --output NEW_DIRECTORY"},
            {"schema", "Return the version 1/2 project JSON schema"},
            {"script-api", "Generated Luau declarations and function metadata"},
            {"check", "Offline Luau checking across registered modules and scenes"},
            {"profile", "simulate with machine-readable fixed-step timing and subsystem metrics"},
            {"play", "Player mode: game inputs, no source watching or developer shortcuts"},
            {"save", "Save running game: --slot NAME (default quick)"},
            {"load", "Restore running game: --slot NAME (default quick)"},
            {"validate", "Parse scene, compile Luau, initialize physics: --project DIR"},
            {"inspect", "Project summary; --live reads the preview state with freshness"},
            {"simulate", "Headless fixed steps: --steps N [--input timeline.json] [--output result.json]"},
            {"patch", "Revision-checked scene edit: --file patch.json"},
            {"checkpoint", "Save project, script, and all registered asset sources: --name NAME"},
            {"restore", "Restore a checkpoint: --name NAME"},
            {"preview", "Interactive preview: [--frames N] [--capture image.png] [--no-audio]"},
            {"capture", "Render evidence: --output image.png [--frames N], or --steps N --input timeline.json --ticks 0,N --output-dir DIR; optional --no-overlay --debug collision,navigation,targeting,camera"},
            {"control", "Control a live preview: --action pause|resume|reset|snapshot|step|capture [--steps N] [--input input.json]"},
            {"new", "Copy a starter: --dimension 2d|3d or --template rockets-2d|rockets-3d|adventure|strategy|shared-2d|workflow-2d --output NEW_DIRECTORY"},
            {"asset schema", "Recipe schema: --type sprite|mesh|music|model"},
            {"asset list", "Compile and describe registered assets: --project DIR"},
            {"asset inspect", "Describe one compiled asset: --project DIR --id ID"},
            {"asset build", "Generate PNG/OBJ/WAV and metadata under .langmake/assets: --project DIR [--id ID]"},
            {"asset render", "Sprite sheet PNG, mesh preview PNG, or music WAV: --project DIR --id ID --output FILE [--scale 1..8] [--seconds N]"},
            {"asset export", "Native sprite PNG, mesh OBJ, or music WAV: --project DIR --id ID --output FILE"},
            {"asset replace", "Validate and atomically replace registered recipe: --project DIR --id ID --file recipe.json --expected-revision REV"}
        }},
        {"controls", "A/D or arrows move; W/S move in 3D; Space jumps; P pauses; R resets; F5 reloads; RMB orbits; wheel zooms; F12 captures; Escape closes"}};
}

void protect_sources(const lm::Project& project, const std::filesystem::path& output) {
    const auto resolved = std::filesystem::weakly_canonical(std::filesystem::absolute(output));
    auto check = [&](const std::filesystem::path& source) {
        if (std::filesystem::weakly_canonical(source) == resolved ||
            (std::filesystem::exists(source) && std::filesystem::exists(resolved) && std::filesystem::equivalent(source, resolved)))
            throw std::runtime_error("Asset output must not overwrite an authored source");
    };
    check(project.file); check(project.script_file);
    for(const auto& [file,bytes]:project.sources)check(project.root/file);
    for (const auto& [id, asset] : project.assets) {
        check(asset.path);
        for (const auto& [file, bytes] : asset.inputs) check(project.root / file);
    }
}
Json export_asset(const lm::Project& project, const lm::CompiledAssets& compiled, const std::string& id,
    const std::filesystem::path& output, int scale, double seconds, bool render, const std::filesystem::path& executable) {
    if (!project.assets.contains(id)) throw std::runtime_error("Unknown asset: " + id);
    protect_sources(project, output);
    auto sidecar = output; sidecar += ".json"; protect_sources(project, sidecar);
    auto result = lm::inspect_asset(project, compiled, id);
    const auto type = project.assets.at(id).type;
    auto extension = output.extension().string();
    if (type == "sprite") {
        if (extension != ".png") throw std::runtime_error("Sprite output must end in .png");
        lm::write_sprite_sheet(*compiled.sprites.at(id), output, scale);
        result["scale"] = scale;
    } else if (type == "music") {
        if (extension != ".wav") throw std::runtime_error("Music output must end in .wav");
        result["audio"] = lm::render_music_wav(compiled.music.at(id), output, seconds);
    } else if (render) {
        if (extension != ".png") throw std::runtime_error("Mesh render output must end in .png; use asset export for OBJ");
        auto model = project;
        model.dimension = "3d"; model.gravity = {0,0,0}; model.music.clear();
        model.assets = {{id, project.assets.at(id)}};
        model.script_source = "return {}";
        model.ui=Json::object();model.scenes.clear();model.modules.clear();model.prefabs=Json::object();
        for(auto key:{"ui","game_camera","navigation","state","audio","scenes","entry_scene"})model.document.erase(key);
        model.root = project.root / ".langmake/previews" / id;
        model.title = "Asset / " + id;
        model.camera_position = {7,5,10}; model.camera_target = {0,0,0};
        auto posed=type=="model"?lm::model_mesh_at(*compiled.models.at(id)):lm::MeshAsset{};
        const auto& mesh=type=="model"?posed:*compiled.meshes.at(id);
        lm::Entity entity; entity.id = id; entity.asset = id; entity.body = "none"; entity.color = {1,1,1,1};
        float longest = 0;
        for (int k=0;k<3;++k) longest = std::max(longest, mesh.maximum[k]-mesh.minimum[k]);
        for (int k=0;k<3;++k) entity.size[k] = type=="model" ? 5/std::max(.01f,longest) : std::max(.01f,5*(mesh.maximum[k]-mesh.minimum[k])/std::max(.01f,longest));
        model.entities = {entity};
        lm::PreviewOptions options; options.frames = 1; options.audio = false; options.watch_source = false; options.overlay = false; options.paused = true;
        options.capture = output; options.executable_directory = executable;
        result["preview"] = lm::preview(std::move(model), options);
    } else {
        if (extension != ".obj") throw std::runtime_error("Mesh export output must end in .obj");
        if(type=="model")lm::write_mesh_obj(lm::model_mesh_at(*compiled.models.at(id)),output);
        else lm::write_mesh_obj(*compiled.meshes.at(id), output);
    }
    result["output"] = std::filesystem::absolute(output).string();
    // Mesh render's capture sidecar carries exact camera/world state; retain it.
    if ((type == "mesh" || type == "model") && render) {
        auto evidence = read_json(sidecar); evidence["asset"] = result;
        lm::write_json_atomic(sidecar, evidence);
    } else lm::write_json_atomic(sidecar, result);
    return result;
}
}

int main(int argc, char** argv) {
    try {
        if(argc>1){const std::set<std::string> commands{"context","target","deploy","operation","verify","receipt","handoff","session"};if(commands.contains(argv[1]))return workflow_command(argc,argv);}
        Arguments args(argc, argv);
        Json result;
        auto command = args.command;
        auto path = std::filesystem::path(args.get("--project", "examples/3d"));
        auto executable = std::filesystem::absolute(argv[0]).parent_path();
        if (command == "help" || command == "--help" || command == "-h") { args.allow({}); result = help(); }
        else if (command == "capabilities") { args.allow({}); result = lm::capabilities(); }
        else if (command == "schema") { args.allow({}); result = lm::project_schema(); }
        else if(command=="script-api"){args.allow({});result=lm::script_api();result["definitions"]=lm::script_types();}
        else if(command=="check"){args.allow({"--project","--profile"});result=lm::check_scripts(lm::load_project(path,args.get("--profile")));}
        else if(command=="save-check"){
            args.allow({"--project","--profile","--file"});if(args.get("--file").empty())throw std::runtime_error("save-check requires --file SAVE.json");
            result=lm::save_compatibility_report(lm::load_project(path,args.get("--profile")),read_json(args.get("--file")));
        }
        else if(command=="ui-check"){
            args.allow({"--project","--profile","--width","--height","--density","--safe-area","--text","--output"});
            auto project=lm::load_project(path,args.get("--profile"));const int width=args.number("--width",1280,8192),height=args.number("--height",800,8192);
            if(width<1||height<1)throw std::runtime_error("UI viewport dimensions must be positive");
            const float density=args.real("--density",1,.25,8);const auto safe=safe_rectangle(args,width,height);
            lm::UiRenderer ui(project,executable,true);ui.set_safe_area(safe);ui.update(project.ui.value("data",Json::object()),0,width,height,density);
            const auto diagnostics=ui.diagnostics(args.get("--text",project.ui.value("expected_characters",std::string())));
            bool ok=true;for(const auto& diagnostic:diagnostics)if(diagnostic.value("severity",std::string())=="error")ok=false;
            result={{"ok",ok},{"source_revision",project.revision},{"profile",args.get("--profile")},{"layout",ui.inspect()},{"diagnostics",diagnostics}};
            if(!args.get("--output").empty()){protect_sources(project,args.get("--output"));lm::write_json_atomic(args.get("--output"),result);}
        }
        else if (command == "asset-schema") {
            args.allow({"--type"});
            auto type = args.get("--type");
            result = type == "music" ? lm::music_schema() : lm::asset_schema(type);
        } else if (command == "asset-replace") {
            args.allow({"--project", "--profile", "--id", "--file", "--expected-revision"});
            if (args.get("--id").empty() || args.get("--file").empty() || args.get("--expected-revision").empty()) throw std::runtime_error("asset replace requires --id, --file, and --expected-revision");
            result = lm::replace_asset(path, args.get("--id"), args.get("--expected-revision"), read_json(args.get("--file")),args.get("--profile"));
        } else if (command == "asset-list" || command == "asset-inspect" || command == "asset-build" || command == "asset-render" || command == "asset-export") {
            if (command == "asset-list") args.allow({"--project"});
            else if (command == "asset-inspect" || command == "asset-build") args.allow({"--project", "--id"});
            else args.allow({"--project", "--id", "--output", "--scale", "--seconds"});
            auto project = lm::load_project(path);
            auto compiled = lm::compile_assets(project);
            auto id = args.get("--id");
            if (command == "asset-list") {
                result = {{"ok",true},{"source_revision",project.revision},{"assets",Json::array()}};
                for (const auto& [name, source] : project.assets) result["assets"].push_back(lm::inspect_asset(project, compiled, name));
            } else if (command == "asset-inspect") result = lm::inspect_asset(project, compiled, id);
            else if (command == "asset-build") {
                if (!id.empty() && !project.assets.contains(id)) throw std::runtime_error("Unknown asset: " + id);
                result = {{"ok",true},{"source_revision",project.revision},{"assets",Json::array()}};
                for (const auto& [name, source] : project.assets) if (id.empty() || id == name) {
                    auto extension = source.type == "sprite" ? ".png" : source.type == "mesh" || source.type=="model" ? ".obj" : ".wav";
                    auto output = project.root / ".langmake/assets" / name / source.revision / (name + extension);
                    result["assets"].push_back(export_asset(project, compiled, name, output, 1, 0, false, executable));
                }
            } else {
                if (id.empty() || args.get("--output").empty()) throw std::runtime_error("Asset render/export requires --id and --output");
                int scale = args.number("--scale", 1, 8);
                if (scale < 1) throw std::runtime_error("scale must be 1..8");
                result = export_asset(project, compiled, id, args.get("--output"), scale, args.number("--seconds", 0, 120), command == "asset-render", executable);
            }
        }
        else if (command == "cook") {
            args.allow({"--project", "--output", "--target", "--profile"});
            if (args.get("--output").empty()) throw std::runtime_error("cook requires --output NEW_DIRECTORY");
            auto project = lm::load_project(path,args.get("--profile"));
            lm::UiRenderer checked_ui(project,executable,true);
            for (const auto& [id, scene] : project.scenes) { lm::UiRenderer checked_scene(*scene,executable,true); }
            result = lm::cook_project(path, args.get("--output"), args.get("--target", "android"),args.get("--profile"));
        }
        else if (command == "validate") {
            args.allow({"--project","--profile"});
            auto project = lm::load_project(path,args.get("--profile")); lm::World world(project);
            lm::UiRenderer checked_ui(project,executable,true);
            for(auto&[id,scene]:project.scenes){auto scene_project=*scene;scene_project.scenes=project.scenes;lm::World checked_world(scene_project);lm::UiRenderer checked_scene_ui(scene_project,executable,true);}
            result = {{"ok", true}, {"revision", project.revision}, {"entities", project.entities.size()}, {"dimension", project.dimension}, {"script", project.script_file.string()}, {"diagnostics", Json::array()}};
        } else if (command == "inspect") {
            args.allow({"--project", "--profile", "--live", "--check-source", "--ids", "--fields", "--role", "--visible", "--offset", "--limit"});
            if (args.get("--live").empty()) result = lm::filter_observation(lm::project_summary(lm::load_project(path,args.get("--profile"))),observation_query(args));
            else {
                auto live = project_directory(path) / ".langmake/live.json";
                result = read_json(live);
                auto age = std::max<std::int64_t>(0,lm::wall_time_ms()-result.value("published_at_ms",std::int64_t(0)));
                result["fresh"] = age < 2500 && result.value("running", false);
                result["age_ms"] = age;
                result["source_checked"] = !args.get("--check-source").empty();
                if(!args.get("--check-source").empty()) try {
                    auto authored = lm::load_project(path,args.get("--profile"));
                    result["source_valid"] = true;
                    result["authored_revision"] = authored.revision;
                    result["matches_source"] = result.value("source_revision", "") == authored.revision;
                } catch (const std::exception& e) {
                    result["source_valid"] = false;
                    result["authored_revision"] = nullptr;
                    result["matches_source"] = false;
                    result["source_diagnostic"] = e.what();
                }
                result["ok"] = true;
            }
            result=lm::filter_observation(std::move(result),observation_query(args));
        } else if (command == "simulate" || command=="profile") {
            args.allow({"--project", "--profile", "--summary", "--steps", "--input", "--output", "--load-save", "--save-output", "--audio-output", "--width", "--height", "--density", "--safe-area"});
            auto project = lm::load_project(path,args.get("--profile")); lm::World world(project);
            if(!args.get("--width").empty()||!args.get("--height").empty()||!args.get("--density").empty()||!args.get("--safe-area").empty()) {
                const auto width=args.number("--width",1280,8192),height=args.number("--height",720,8192);
                world.set_viewport(width,height,args.real("--density",1,.25,8),safe_rectangle(args,width,height));
            }
            if(!args.get("--load-save").empty())world.restore(read_json(args.get("--load-save")));
            for(auto option:{"--save-output","--audio-output"})if(!args.get(option).empty())protect_sources(project,args.get(option));
            if (!args.get("--output").empty()) protect_sources(project, args.get("--output"));
            int steps = args.number("--steps", 120, 36000);
            lm::Timeline timeline(args.get("--input").empty()?Json::object():read_json(args.get("--input")));
            auto start = std::chrono::steady_clock::now();
            std::vector<double> timings;timings.reserve(steps);
            const bool audio_requested=!args.get("--audio-output").empty();
            Json audio_events=Json::array();std::uint64_t last_audio=0;
            const auto audio_origin=world.presentation().at("tick").get<std::uint64_t>();
            std::map<std::string,std::shared_ptr<const lm::MusicScore>> audio_scores;
            std::map<std::string,Json> audio_configurations,audio_bus_states;
            std::map<std::string,std::string> audio_music,audio_music_bus;
            std::map<std::string,bool> audio_music_loop;
            std::string mixer_scene;
            auto remember_audio_scene=[&] {
                const auto& scene=world.current_project();const auto& id=scene.scene_id;
                for(const auto& [asset,score]:world.assets().music)audio_scores[id+"::"+asset]=score;
                if(mixer_scene!=id) {
                    const auto state=world.snapshot();
                    audio_configurations[id]=scene.document.value("audio",Json::object());
                    audio_bus_states[id]=state.value("audio_buses",Json::object());
                    const auto music=state.value("music",scene.music);audio_music[id]=music.empty()?"":id+"::"+music;
                    audio_music_bus[id]=state.value("music_bus",std::string("music"));audio_music_loop[id]=state.value("music_loop",true);
                }
            };
            auto configure_audio_scene=[&](const std::string& scene,std::uint64_t tick) {
                audio_events.push_back({{"tick",tick},{"action","configure"},{"configuration",audio_configurations.at(scene)},{"music",audio_music.at(scene)},
                    {"music_bus",audio_music_bus.at(scene)},{"music_loop",audio_music_loop.at(scene)}});
                for(const auto& [id,bus]:audio_bus_states.at(scene).items())audio_events.push_back({{"tick",tick},{"action","bus"},{"bus",id},
                    {"gain",bus.value("gain",1.f)},{"pan",bus.value("pan",0.f)},{"mute",bus.value("mute",false)}});
                mixer_scene=scene;
            };
            auto collect_audio=[&] {
                if(!audio_requested)return;
                remember_audio_scene();
                for(auto event:world.audio_events()) {
                    const auto serial=event.at("serial").get<std::uint64_t>();if(serial<=last_audio)continue;last_audio=serial;
                    const auto absolute_tick=event.at("tick").get<std::uint64_t>();if(absolute_tick<audio_origin)continue;
                    const auto tick=absolute_tick-audio_origin;const auto scene=event.value("scene",world.current_project().scene_id);
                    if(scene!=mixer_scene)configure_audio_scene(scene,tick);
                    event["tick"]=tick;
                    if(event.contains("asset")&&!event.at("asset").get<std::string>().empty())event["asset"]=scene+"::"+event.at("asset").get<std::string>();
                    audio_events.push_back(std::move(event));
                }
                if(mixer_scene!=world.current_project().scene_id)configure_audio_scene(world.current_project().scene_id,world.presentation().at("tick").get<std::uint64_t>()-audio_origin);
            };
            if(audio_requested){remember_audio_scene();configure_audio_scene(world.current_project().scene_id,0);collect_audio();}
            for (int tick = 0; tick < steps; ++tick) {
                auto begin=std::chrono::steady_clock::now();
                world.step(1.0f / 60.0f, timeline.at(tick));
                timings.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
                collect_audio();
            }
            result = {{"ok", true}, {"steps", steps}, {"fixed_dt", 1.0 / 60.0}, {"source_revision", project.revision}, {"state", world.snapshot()},
                {"elapsed_ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()}};
            std::sort(timings.begin(),timings.end());
            result["profile"]={{"step_p50_ms",timings.empty()?0:timings[std::min(timings.size()-1,timings.size()/2)]},{"step_p95_ms",timings.empty()?0:timings[std::min(timings.size()-1,timings.size()*95/100)]},{"step_max_ms",timings.empty()?0:timings.back()},{"runtime",world.statistics()}};
            if(!args.get("--save-output").empty())lm::write_json_atomic(args.get("--save-output"),world.save());
            if(audio_requested)result["audio"]=lm::render_game_audio_wav(audio_scores,audio_events,steps,args.get("--audio-output"));
            if (!args.get("--output").empty()) lm::write_json_atomic(args.get("--output"), result);
            if(!args.get("--summary").empty()){
                result["summary"]={{"tick",result["state"]["tick"]},{"scene",result["state"].value("scene",std::string())},{"won",result["state"].value("won",false)}};
                result.erase("state");result["output"]=args.get("--output");
            }
        } else if (command == "patch") {
            args.allow({"--project", "--profile", "--file"});
            if (args.get("--file").empty()) throw std::runtime_error("patch requires --file");
            result = lm::patch_project(path, read_json(args.get("--file")),args.get("--profile"));
        } else if (command == "checkpoint" || command == "restore") {
            args.allow({"--project", "--name"});
            if (args.get("--name").empty()) throw std::runtime_error("Checkpoint --name is required");
            result = command == "checkpoint" ? lm::checkpoint_project(path, args.get("--name")) : lm::restore_project(path, args.get("--name"));
        } else if (command == "preview" || command == "capture" || command == "play") {
            args.allow({"--project", "--profile", "--density", "--safe-area", "--frames", "--capture", "--output", "--no-audio", "--width", "--height", "--no-overlay", "--input", "--steps", "--ticks", "--output-dir", "--debug"});
            lm::PreviewOptions options;
            options.player=command=="play";
            if(!args.get("--debug").empty()){
                if(options.player)throw std::runtime_error("Debug layers are available in preview/capture, not play");
                std::istringstream layers(args.get("--debug"));std::string layer;
                while(std::getline(layers,layer,',')){if(layer!="collision"&&layer!="navigation"&&layer!="targeting"&&layer!="camera")throw std::runtime_error("Unknown debug layer: "+layer);options.debug.push_back(layer);}
            }
            options.overlay=!options.player&&args.get("--no-overlay").empty();
            options.watch_source=command=="preview";
            options.isolated=command=="capture";
            if(!args.get("--steps").empty()){
                if(command!="capture"||!args.get("--frames").empty())throw std::runtime_error("Timeline --steps requires capture and cannot be combined with --frames");
                options.steps=args.number("--steps",0,36000);options.timeline=args.get("--input").empty()?Json::object():read_json(args.get("--input"));
                lm::Timeline validate_timeline(options.timeline);
                options.capture_directory=args.get("--output-dir");if(options.capture_directory.empty())throw std::runtime_error("Tick captures require --output-dir");
                std::istringstream ticks(args.get("--ticks",std::to_string(options.steps)));std::string tick;
                while(std::getline(ticks,tick,',')){size_t count;int n=std::stoi(tick,&count);if(count!=tick.size()||n<0||n>options.steps||(!options.capture_ticks.empty()&&n<=options.capture_ticks.back()))throw std::runtime_error("Capture ticks must be unique, increasing and within steps");options.capture_ticks.push_back(n);}
                if(options.capture_ticks.empty()||options.capture_ticks.size()>256)throw std::runtime_error("Require 1..256 capture ticks");
            }else if(!args.get("--input").empty()||!args.get("--ticks").empty()||!args.get("--output-dir").empty())throw std::runtime_error("Timeline options require --steps");
            options.frames = args.number("--frames", command == "capture" ? 30 : 0, 360000);
            if (command == "capture" && options.frames == 0) throw std::runtime_error("capture requires a positive --frames value");
            options.width = args.number("--width", 1280, 3840);
            options.height = args.number("--height", 800, 2160);
            options.density=args.real("--density",0,.25,8);options.safe_area=safe_rectangle(args,options.width,options.height);
            if (options.width < 640 || options.height < 400) throw std::runtime_error("Minimum preview size is 640x400");
            options.audio = command != "capture" && args.get("--no-audio").empty();
            options.capture = args.get(command == "capture" ? "--output" : "--capture");
            if(options.steps>=0&&(!args.get("--output").empty()||!args.get("--capture").empty()))throw std::runtime_error("Tick captures use --output-dir; do not mix --output or --capture");
            if (command == "capture" && options.capture.empty() && options.steps<0) throw std::runtime_error("capture requires --output image.png");
            options.executable_directory = executable;
            auto project = lm::load_project(path,args.get("--profile"));
            if (!options.capture.empty()) {
                protect_sources(project, options.capture);
                auto sidecar = options.capture; sidecar += ".json";
                protect_sources(project, sidecar);
            }
            for(int tick:options.capture_ticks){auto image=options.capture_directory/("tick-"+std::to_string(tick)+".png");protect_sources(project,image);image+=".json";protect_sources(project,image);}
            result = lm::preview(std::move(project), options);
        } else if (command == "control" || command=="save" || command=="load") {
            args.allow({"--project", "--action", "--steps", "--input", "--include-state", "--ids", "--fields", "--role", "--visible", "--offset", "--limit", "--slot"});
            auto root = project_directory(path);
            auto action = command=="control"?args.get("--action"):command;
            const std::vector<std::string> actions{"pause", "resume", "reset", "snapshot", "step", "capture", "save", "load"};
            if (std::find(actions.begin(), actions.end(), action) == actions.end()) throw std::runtime_error("Unknown control action");
            auto live = read_json(root / ".langmake/live.json");
            auto age=lm::wall_time_ms()-live.value("published_at_ms",std::int64_t(0));
            if (!live.value("running", false) || age > 3000) throw std::runtime_error("No fresh running preview for this project");
            auto id = std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count())+std::to_string(std::random_device{}());
            Json request{{"id", id}, {"session", live.at("session")}, {"action", action}, {"steps", args.number("--steps", 1, 600)}};
            request["expires_at_ms"]=lm::wall_time_ms()+10000;
            request["include_state"]=!args.get("--include-state").empty();request["query"]=observation_query(args);
            if(action=="save"||action=="load"){auto slot=args.get("--slot","quick");if(slot.empty()||slot.size()>64||slot.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid save slot");request["slot"]=slot;}
            if (!args.get("--input").empty()) { auto input = read_json(args.get("--input")); read_input(input); request["input"] = input; }
            auto request_path = root / ".langmake/commands" / (id + ".json");
            auto result_path = root / ".langmake/results" / (id + ".json");
            lm::write_json_atomic(request_path, request);
            bool received = false;
            for (int i = 0; i < 200; ++i) {
                if (std::filesystem::exists(result_path)) { result = read_json(result_path); received = true; std::error_code ignored;std::filesystem::remove(result_path,ignored);break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (!received) throw std::runtime_error("Preview control timed out; request ID " + id + " remains inspectable in .langmake");
        } else if (command == "new") {
            args.allow({"--dimension", "--template", "--output"});
            auto dimension = args.get("--dimension", "3d");
            if (dimension != "2d" && dimension != "3d") throw std::runtime_error("dimension must be 2d or 3d");
            if (args.get("--output").empty()) throw std::runtime_error("new requires --output NEW_DIRECTORY");
            std::filesystem::path output(args.get("--output"));
            if (std::filesystem::exists(output)) throw std::runtime_error("Destination already exists; refusing to overwrite it");
            auto starter_name = args.get("--template", dimension);
            if (starter_name != "2d" && starter_name != "3d" && starter_name != "rockets-2d" && starter_name != "rockets-3d" && starter_name != "adventure" && starter_name != "strategy" && starter_name != "shared-2d" && starter_name != "workflow-2d") throw std::runtime_error("Unknown starter template");
            if (!args.get("--template").empty() && !args.get("--dimension").empty()) throw std::runtime_error("Choose --template or --dimension");
            auto source = executable / "examples" / starter_name;
            if (!std::filesystem::exists(source)) source = executable.parent_path() / "examples" / starter_name;
            auto starter = lm::load_project(source);
            lm::copy_project_sources(starter, output);
            if(starter.document.value("schema_version",1)==2){
                auto manifest=read_json(output/"project.json");const auto identity=std::to_string(lm::wall_time_ms())+std::to_string(std::random_device{}());manifest["project_id"]="game-"+identity;
                const std::string application_id="org.langmake.game.g"+identity;
                auto reset_application_id=[&](Json& application){if(!application.is_object())return;if(application.contains("id"))application["id"]=application_id;
                    if(application.contains("platforms"))for(auto& [platform,values]:application["platforms"].items())if(values.contains("id"))values["id"]=application_id;};
                if(manifest.contains("application"))reset_application_id(manifest["application"]);
                if(manifest.contains("profiles"))for(auto& [name,values]:manifest["profiles"].items())if(values.contains("application"))reset_application_id(values["application"]);
                lm::write_json_atomic(output/"project.json",manifest);
            }
            if(std::filesystem::is_regular_file(source/"README.md")&&!std::filesystem::exists(output/"README.md"))std::filesystem::copy_file(source/"README.md",output/"README.md");
            auto created = lm::load_project(output);
            result = {{"ok", true}, {"project", created.file.string()}, {"revision", created.revision}};
        } else throw std::runtime_error("Unknown command: " + command + ". Run langmake help.");
        std::cout << result.dump(2) << '\n';
        return result.is_object() && result.contains("ok") && result["ok"] == false ? 1 : 0;
    } catch (const std::exception& error) {
        std::cout << exception_result(error).dump(2) << '\n';
        return 1;
    }
}
