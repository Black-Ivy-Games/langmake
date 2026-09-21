#include "langmake/preview.hpp"
#include "langmake/project.hpp"
#include "langmake/io.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <charconv>
#include <set>
#include <stdexcept>

namespace {
using Path=std::filesystem::path;
Path contained(const Path& root,const std::string& text) {
    Path input(text);if(!input.is_absolute())throw std::runtime_error("Android runtime paths must be absolute");
    auto target=std::filesystem::weakly_canonical(input),base=std::filesystem::weakly_canonical(root);
    auto relative=target.lexically_relative(base);
    if(relative.empty()||relative.is_absolute()||std::any_of(relative.begin(),relative.end(),[](const auto& part){return part=="..";}))throw std::runtime_error("Android runtime path escapes private application storage");
    return target;
}
int integer(const std::string& value,int minimum,int maximum) {
    int result=0;auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    if(parsed.ec!=std::errc()||parsed.ptr!=value.data()+value.size()||result<minimum||result>maximum)throw std::runtime_error("Android numeric argument is outside its supported range");
    return result;
}
}

// SDLActivity loads this symbol from libmain.so. SDL's JNI entry points are
// statically linked into that same library; no separate engine or CLI process.
extern "C" SDL_DECLSPEC int SDL_main(int argc,char** argv) {
    Path storage;
    try {
        const char* internal=SDL_GetAndroidInternalStoragePath();
        if(!internal||!*internal)throw std::runtime_error("Android private storage is unavailable");
        const Path private_root(internal);std::map<std::string,std::string> arguments;std::set<std::string> flags;
        const std::set<std::string> valued{"--project","--runtime","--storage","--frames","--capture","--steps","--input","--ticks","--output-dir"};
        const std::set<std::string> boolean{"--development","--fresh","--no-audio"};
        for(int i=1;i<argc;++i){std::string key(argv[i]);if(arguments.contains(key)||flags.contains(key))throw std::runtime_error("Duplicate Android launch argument: "+key);if(boolean.contains(key))flags.insert(key);else if(valued.contains(key)&&i+1<argc)arguments[key]=argv[++i];else throw std::runtime_error("Unknown or incomplete Android launch argument: "+key);}
        for(const char* key:{"--project","--runtime","--storage"})if(!arguments.contains(key))throw std::runtime_error(std::string("Missing Android launch argument: ")+key);
        const auto candidate_storage=contained(private_root/"state",arguments.at("--storage"));
        const auto project_path=contained(private_root,arguments.at("--project"));
        const auto runtime=contained(private_root,arguments.at("--runtime"));
        // Storage must be independent of immutable packaged content/resources.
        for(const auto& source:{project_path.parent_path(),runtime}) {
            const auto relative=candidate_storage.lexically_relative(source);
            if(!relative.empty()&&!relative.is_absolute()&&std::none_of(relative.begin(),relative.end(),[](const auto& part){return part=="..";}))throw std::runtime_error("Writable Android storage must be outside packaged content");
        }
        storage=candidate_storage;
        bool debug_arguments=!flags.empty();for(const auto&[key,value]:arguments)if(key!="--project"&&key!="--runtime"&&key!="--storage")debug_arguments=true;
#if !defined(LANGMAKE_ANDROID_DEVELOPMENT) || !LANGMAKE_ANDROID_DEVELOPMENT
        if(debug_arguments)throw std::runtime_error("Development launch arguments are unavailable in this Android build");
#endif
        lm::PreviewOptions options;options.executable_directory=runtime;options.storage_directory=storage;
        options.watch_source=false;options.player=!flags.contains("--development");options.overlay=false;
        options.audio=!flags.contains("--no-audio");options.restore_recovery=!flags.contains("--fresh");
        if(arguments.contains("--frames"))options.frames=integer(arguments.at("--frames"),1,36000);
        if(arguments.contains("--capture")){if(options.frames==0)throw std::runtime_error("Android --capture requires --frames");options.capture=contained(storage,arguments.at("--capture"));}
        if(arguments.contains("--steps")) {
            if(options.frames||!arguments.contains("--input")||!arguments.contains("--ticks")||!arguments.contains("--output-dir"))throw std::runtime_error("Android timeline capture requires steps, input, ticks, output-dir and no frames");
            options.steps=integer(arguments.at("--steps"),0,36000);options.isolated=true;
            options.timeline=lm::read_json_shared(contained(private_root,arguments.at("--input")),16*1024*1024);
            options.capture_directory=contained(storage,arguments.at("--output-dir"));
            const auto& ticks=arguments.at("--ticks");std::size_t start=0;
            while(start<=ticks.size()){auto end=ticks.find(',',start);if(end==std::string::npos)end=ticks.size();options.capture_ticks.push_back(integer(ticks.substr(start,end-start),0,options.steps));if(options.capture_ticks.size()>128)throw std::runtime_error("Android capture accepts at most 128 ticks");if(end==ticks.size())break;start=end+1;}
            if(!std::is_sorted(options.capture_ticks.begin(),options.capture_ticks.end())||std::adjacent_find(options.capture_ticks.begin(),options.capture_ticks.end())!=options.capture_ticks.end())throw std::runtime_error("Android capture ticks must be increasing and unique");
        } else if(arguments.contains("--input")||arguments.contains("--ticks")||arguments.contains("--output-dir"))throw std::runtime_error("Android timeline arguments require --steps");
        if(options.frames||options.steps>=0)options.restore_recovery=false;
        auto project=lm::load_project(project_path);
        std::filesystem::create_directories(storage);
        lm::write_json_atomic(storage/"launch-result.json",{{"ok",true},{"running",true},{"version","0.5.0"},{"platform","android"},{"source_revision",project.revision}});
        auto result=lm::preview(std::move(project),options);result["version"]="0.5.0";result["platform"]="android";
        lm::write_json_atomic(storage/"launch-result.json",result);
        return 0;
    } catch(const std::exception& error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Langmake Android: %s",error.what());
        if(!storage.empty())try{lm::write_json_atomic(storage/"launch-result.json",{{"ok",false},{"running",false},{"version","0.5.0"},{"platform","android"},{"diagnostic",error.what()}});}catch(...){}
        return 1;
    }
}
