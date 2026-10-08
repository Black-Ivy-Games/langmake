#include "langmake/project.hpp"
#include "langmake/preview.hpp"
#include <SDL3/SDL.h>
#include <mach-o/dyld.h>
#include <iostream>
#include <vector>

// The exported app owns immutable Resources; saves never modify its signature.
int main(int, char**) {
    try {
        uint32_t length=0;
        _NSGetExecutablePath(nullptr,&length);
        std::vector<char> name(length);
        if(_NSGetExecutablePath(name.data(),&length)!=0)throw std::runtime_error("Cannot locate the app bundle");
        const auto resources=std::filesystem::canonical(name.data()).parent_path().parent_path()/"Resources";
        auto project=lm::load_project(resources/"game/project.json");
        const auto id=project.document.value("project_id",std::string("game"));
        if(id.empty()||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")!=std::string::npos)
            throw std::runtime_error("Invalid persistent game identifier");
        char* preference=SDL_GetPrefPath("Langmake",id.c_str());
        if(!preference)throw std::runtime_error(SDL_GetError());
        lm::PreviewOptions options;
        options.storage_directory=preference;SDL_free(preference);
        options.executable_directory=resources;
        options.player=true;options.watch_source=false;options.overlay=false;
        options.width=640;options.height=960;
        lm::preview(std::move(project),options);
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Unable to start game",error.what(),nullptr);
        return 1;
    }
}
