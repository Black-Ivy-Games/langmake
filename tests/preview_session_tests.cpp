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
    // Failure reporting must use passive getters. A bound provider which fails
    // after the original update must neither replace it nor prevent the receipt.
    auto fault_project=project;fault_project.script_source=R"(local failed=false
      return {start=function() engine.state_bind('view',function() if failed then error('provider must not run') end return 42 end) end,
              update=function() failed=true;error('original update failure') end})";
    auto fault_options=options;fault_options.storage_directory=output/"script-fault";
    bool fault_reported=false;try{run(fault_project,fault_options);}catch(const std::exception& error){
        fault_reported=std::string(error.what()).find("original update failure")!=std::string::npos;}
    auto fault_record=lm::read_json_shared(fault_options.storage_directory/"script-failure.json",32*1024);
    check(fault_record.at("script_diagnostic").at("phase")=="update"&&
          fault_record.at("diagnostic").get<std::string>().find("original update failure")!=std::string::npos,
          "Preview lost the original update failure while observing the failed world");
    check(fault_record.at("recovered")==false&&fault_reported,"Bounded preview did not report the unrecovered script failure");
    auto recover_project=project;recover_project.script_source=R"(return {
      update=function() if not engine.state_get('recovered',false) then engine.state_set('partial',true);error('original recoverable failure') end end,
      on_recover=function() engine.state_set('recovered',true) end})";
    auto recover_options=options;recover_options.storage_directory=output/"script-recovery";
    const auto recovered=run(recover_project,recover_options);
    const auto recover_record=lm::read_json_shared(recover_options.storage_directory/"script-failure.json",32*1024);
    check(recover_record.at("recovered")==true&&recovered.at("state").at("state").at("recovered")==true&&
          !recovered.at("state").at("state").contains("partial"),"Preview recovery reused partial failed gameplay state");
    recover_project.script_source="return {update=function() error('original before failed recovery') end,on_recover=function() error('recovery hook failure') end}";
    recover_options.storage_directory=output/"failed-script-recovery";
    bool recovery_reported=false;try{run(recover_project,recover_options);}catch(const std::exception& error){
        recovery_reported=std::string(error.what()).find("original before failed recovery")!=std::string::npos;}
    const auto failed_recovery=lm::read_json_shared(recover_options.storage_directory/"script-failure.json",32*1024);
    check(recovery_reported&&failed_recovery.at("recovered")==false&&failed_recovery.at("script_diagnostic").at("phase")=="update"&&
          failed_recovery.at("recovery_diagnostic").get<std::string>().find("recovery hook failure")!=std::string::npos,
          "Recovery failure replaced the original script diagnostic");
    {lm::PreviewSession session(project,options,true);check(session.iterate(),"Callback session did not initialize");
     SDL_Event quit{};quit.type=SDL_EVENT_QUIT;session.event(quit);
     check(!session.iterate(),"Callback quit event was not consumed");check(session.result().at("state").at("running")==false,"Quit left the session running");}
    // The generic recovery utility must preserve platform-specific slot names.
    auto save=output/"saves/_ios_resume.json";lm::write_json_atomic(save,{{"invalid_for_restore",true}});
    const auto rejected=lm::quarantine_mobile_recovery(save,"fixture");
    check(rejected.at("preserved")==true&&std::filesystem::exists(output/"saves/_ios_resume.rejected.json"),"iOS rejected recovery was not preserved");
    std::cout<<"PASS callback lifecycle, passive script failure reporting, safe recovery, separate recovery errors and iOS recovery quarantine\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
