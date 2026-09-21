#include "langmake/preview.hpp"
#include "langmake/project.hpp"
#include "langmake/io.hpp"
#include "langmake/android_recovery.hpp"
#include <SDL3/SDL.h>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool result,const char* message){if(!result)throw std::runtime_error(message);}
lm::Json run(lm::Project project,lm::PreviewOptions options){
    lm::PreviewSession session(std::move(project),std::move(options),true);
    for(int iteration=0;session.iterate();++iteration){
        check(iteration<40,"Callback player did not terminate within its bounded frame budget");
        // SDL owns pumping on iOS; emulate that contract from this test host.
        SDL_Event event;while(SDL_PollEvent(&event))session.event(event);
    }
    return session.result();
}
}
int main(int argc,char** argv){try{
    check(argc==3,"Usage: preview_session_tests SOURCE_ROOT BUILD_DIRECTORY");
    const auto root=std::filesystem::absolute(argv[1]);
    const auto output=root/".cache"/("preview-session-"+std::to_string(lm::wall_time_ms()));
    lm::PreviewOptions options;options.executable_directory=std::filesystem::absolute(argv[2]);options.storage_directory=output;
    options.audio=false;options.watch_source=false;options.player=true;options.overlay=false;options.isolated=true;options.frames=3;
    const auto project=lm::load_project(root/"examples/2d");
    const auto result=run(project,options);
    check(result.at("frames")==6,"Callback frame-count completion changed");
    check(result.at("state").at("tick")==3,"Callback frame-count simulation changed");
    // Destruction after initialization must unwind graphics and release the
    // project lock. A subsequent session must immediately reuse the storage.
    options.isolated=false;options.frames=0;
    {lm::PreviewSession session(project,options,true);check(session.iterate(),"Session did not yield after initialization");}
    options.frames=3;check(run(project,options).at("state").at("tick")==3,"Early destruction leaked the runtime lock/resources");
    // Failed graphics resource loading must also close the SDL/bgfx lifecycle.
    auto invalid=options;invalid.executable_directory=output/"missing-resources";
    bool failed=false;try{run(project,invalid);}catch(const std::exception&){failed=true;}
    check(failed,"Missing graphics resources did not report failure");
    check(run(project,options).at("ok")==true,"Session did not recover after initialization failure");
    {lm::PreviewSession session(project,options,true);check(session.iterate(),"Callback session did not initialize");
     SDL_Event quit{};quit.type=SDL_EVENT_QUIT;session.event(quit);
     check(!session.iterate(),"Callback quit event was not consumed");check(session.result().at("state").at("running")==false,"Quit left the session running");}
    // The generic recovery utility must preserve platform-specific slot names.
    auto save=output/"saves/_ios_resume.json";lm::write_json_atomic(save,{{"invalid_for_restore",true}});
    const auto rejected=lm::quarantine_mobile_recovery(save,"fixture");
    check(rejected.at("preserved")==true&&std::filesystem::exists(output/"saves/_ios_resume.rejected.json"),"iOS rejected recovery was not preserved");
    std::cout<<"PASS callback frame/tick parity, early destruction, failed initialization recovery, quit events and iOS recovery quarantine\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
