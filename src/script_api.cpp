#include "langmake/script_api.hpp"
#include <Luau/Ast.h>
#include <Luau/BuiltinDefinitions.h>
#include <Luau/Config.h>
#include <Luau/Frontend.h>
#include <Luau/TypeInfer.h>
#include <Luau/TypeArena.h>
#include <Luau/Error.h>
#include <set>
namespace lm {
Json script_api(){
    Json functions=Json::object();
    for(auto [name,type]:std::initializer_list<std::pair<const char*,const char*>>{
        {"input","(name: string) -> any"},{"input_pressed","(name: string) -> boolean"},{"input_released","(name: string) -> boolean"},{"input_events","() -> {any}"},
        {"move_player","(x: number, z: number?) -> ()"},{"jump","(speed: number) -> boolean"},{"position","(id: string) -> (number,number,number)"},{"distance","(a: string,b: string) -> number"},
        {"collect","(id: string) -> boolean"},{"collected","() -> number"},{"win","() -> ()"},{"message","(text: string) -> ()"},
        {"set_color","(id: string,r: number,g: number,b: number) -> ()"},{"set_position","(id: string,x: number,y: number,z: number) -> ()"},
        {"rotation","(id: string) -> (number,number,number)"},{"angular_velocity","(id: string) -> number"},{"set_angular_velocity","(id: string,degrees_per_second: number) -> ()"},
        {"set_visible","(id: string,visible: boolean) -> ()"},{"set_rotation","(id: string,x: number,y: number,z: number) -> ()"},{"set_velocity","(id: string,x: number,y: number,z: number) -> ()"},
        {"play_animation","(id: string,clip: string,restart: boolean?,fade: number?) -> ()"},{"play_sound","(asset: string,bus: string?) -> ()"},{"play_music","(asset: string,loop: boolean?,bus: string?) -> ()"},{"stop_music","() -> ()"},
        {"state_get","(key: string,default: any?) -> any"},{"state_set","(key: string,value: any) -> ()"},{"spawn","(prefab: string,overrides: any?) -> string"},{"destroy","(id: string) -> boolean"},{"exists","(id: string) -> boolean"},
        {"set_asset","(id: string,asset: string,animation: string?) -> ()"},{"set_visual_scale","(id: string,x: number,y: number,z: number) -> ()"},{"set_opacity","(id: string,opacity: number) -> ()"},{"set_collision_enabled","(id: string,enabled: boolean) -> ()"},
        {"raycast","(x: number,y: number,z: number,x2: number,y2: number,z2: number,options: any?) -> any"},{"shape_cast","(x: number,y: number,z: number,x2: number,y2: number,z2: number,options: any?) -> any"},
        {"overlap","(x: number,y: number,z: number,radius: number,options: any?) -> any"},
        {"ui_set","(id: string,value: any) -> ()"},{"ui_get","(id: string) -> any"},{"ui_event","(id: string,kind: string?) -> {any}"},
        {"change_scene","(id: string) -> ()"},{"scene","() -> string"},{"camera_set","(configuration: any) -> ()"},{"camera_get","() -> any"},{"viewport","() -> any"},
        {"navigation_path","(x: number,y: number,z: number,x2: number,y2: number,z2: number) -> any"},{"navigate","(id: string,x: number,y: number,z: number,speed: number?) -> any"},
        {"navigation_stop","(id: string) -> ()"},{"navigation_status","(id: string) -> any"},{"navigation_rebuild","() -> ()"},
        {"set_audio_bus","(id: string,gain: number,pan: number,mute: boolean) -> ()"},{"random","() -> number"},{"tick","() -> number"},
        {"definitions","() -> any"},{"entities","(role: string?) -> {any}"},{"grounded","(id: string) -> boolean"},
        {"save_game","(slot: string) -> ()"},{"load_game","(slot: string) -> ()"},
        {"animation_events","(id: string) -> {any}"},{"joint_position","(id: string,joint: string) -> (number,number,number)"},{"contacts","(id: string?) -> {any}"}
    })functions[name]=type;
    return {{"ok",true},{"version","0.5.0"},{"functions",functions},
        {"viewport",{{"initial",{{"available",false}}},{"available_fields",{"available","width","height","density","safe_area","logical_size","logical_safe_area"}},
            {"units","width/height/safe_area are physical pixels; logical values divide pixels by density"},
            {"lifecycle","Unavailable during initial script/start; host sets before updates. Current host metrics survive save restore and scene changes; never restored from save."},
            {"semantics","Observation only: camera frustum and gameplay geometry are not resized automatically"}}}};
}
std::string script_types(){std::string text="-- Generated from Langmake's public API metadata.\ndeclare engine: {\n";auto metadata=script_api();for(auto&[name,type]:metadata["functions"].items())text+="    "+name+": "+type.get<std::string>()+",\n";return text+"}\n";}
namespace {
struct Resolver:Luau::FileResolver {
    std::map<std::string,std::string> sources;
    std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& name)override{auto it=sources.find(name);if(it==sources.end())return std::nullopt;return Luau::SourceCode{it->second,Luau::SourceCode::Module};}
    std::optional<Luau::ModuleInfo> resolveModule(const Luau::ModuleInfo*,Luau::AstExpr* expr)override{if(auto* literal=expr->as<Luau::AstExprConstantString>())return Luau::ModuleInfo{std::string(literal->value.data,literal->value.size)};return std::nullopt;}
};
struct Config:Luau::ConfigResolver {Luau::Config config;const Luau::Config& getConfig(const Luau::ModuleName&)const override{return config;}};
}
Json check_scripts(const Project& project){
    Resolver resolver;resolver.sources=project.modules;resolver.sources["@entry"]=project.script_source;
    for(auto&[id,scene]:project.scenes)resolver.sources["@scene:"+id]=scene->script_source;
    Config config;Luau::FrontendOptions options;options.moduleTimeLimitSec=2.0;options.applyInternalLimitScaling=true;
    Luau::Frontend frontend(&resolver,&config,options);Luau::registerBuiltinGlobals(frontend,frontend.globals);
    auto definition=frontend.loadDefinitionFile(frontend.globals,frontend.globals.globalScope,script_types(),"@langmake",false);
    if(!definition.success)throw std::runtime_error("Internal engine type definitions could not be loaded");
    Luau::freeze(frontend.globals.globalTypes);
    Json errors=Json::array();std::set<std::string> seen;
    for(auto&[name,bytes]:resolver.sources){auto checked=frontend.check(name);for(auto& error:checked.errors){
        Json diagnostic{{"code","script_type"},{"module",error.moduleName},{"line",error.location.begin.line+1},{"column",error.location.begin.column+1},{"message",Luau::toString(error,Luau::TypeErrorToStringOptions{&resolver})}};
        if(seen.insert(diagnostic.dump()).second)errors.push_back(std::move(diagnostic));
    }}
    return {{"ok",errors.empty()},{"source_revision",project.revision},{"modules_checked",resolver.sources.size()},{"diagnostics",errors}};
}
}
