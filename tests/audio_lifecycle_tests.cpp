#include "langmake/audio.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace {
void check(bool result,const char* message){if(!result)throw std::runtime_error(message);}
std::uint64_t frames(const lm::Audio& audio){return audio.measurements().at("sample_frames").get<std::uint64_t>();}
bool advances(lm::Audio& audio,std::uint64_t previous){for(int i=0;i<100;++i){if(frames(audio)>previous)return true;std::this_thread::sleep_for(std::chrono::milliseconds(20));}return false;}
}
int main(){try{
    lm::Audio disabled(false);check(disabled.set_suspended(true)&&disabled.set_suspended(false)&&!disabled.available(),"Disabled audio lifecycle must remain safe");
    const auto disabled_state=disabled.measurements();
    check(disabled_state.at("device_initialization_status")=="disabled"&&!disabled_state.at("device_requested").get<bool>()&&disabled_state.at("device_backend")=="none"&&!disabled_state.at("output_available").get<bool>(),"Disabled output must be distinguishable from failed initialization");
    lm::Audio audio(true);const auto state=audio.measurements();
    check(state.at("device_requested").get<bool>()&&state.at("output_available")==audio.available(),"Audio observations must agree with output availability");
#ifdef LM_EXPECT_NULL_AUDIO
    check(state.at("device_is_null").get<bool>(),"Null-only verification must initialize the actual null backend");
#endif
    if(state.at("device_is_null").get<bool>()){
        check(!audio.available()&&state.at("device_ready").get<bool>()&&state.at("device_initialization_status")=="null_backend", "Null backend must never count as usable audio output");
        check(advances(audio,0),"Null backend fixture must actually run the mixer");
        check(audio.set_suspended(true)&&audio.set_suspended(false)&&!audio.available(),"Null backend lifecycle must preserve unavailable output");
        std::cout<<"PASS disabled and null backend observability; SKIP physical device unavailable\n";return 0;
    }
    if(!audio.available()){check(state.at("device_initialization_status")=="init_failed"||state.at("device_initialization_status")=="start_failed","Unavailable output must report its initialization failure");check(state.at("device_last_result")!=0&&!state.at("device_last_error").get<std::string>().empty(),"Audio failure must preserve a result code and diagnostic");std::cout<<"PASS disabled lifecycle; SKIP physical device unavailable: "<<state.dump()<<'\n';return 0;}
    check(state.at("device_initialization_status")=="ready"&&state.at("device_backend")!="none"&&state.at("device_last_result")==0,"Usable output must identify its actual backend and successful startup");
    check(advances(audio,0),"Active audio device did not render");
    check(audio.set_suspended(true),"Audio device did not suspend");const auto stopped=frames(audio);std::this_thread::sleep_for(std::chrono::milliseconds(120));check(frames(audio)==stopped,"Suspended physical device continued rendering");
    check(audio.set_suspended(false)&&advances(audio,stopped),"Resume must preserve and advance the existing mixer sample clock");
    audio.set_paused(true);check(audio.set_suspended(true)&&audio.set_suspended(false),"Paused suspend/resume failed");const auto paused=frames(audio);std::this_thread::sleep_for(std::chrono::milliseconds(120));check(frames(audio)==paused,"Physical resume must preserve logical pause");
    audio.set_paused(false);check(advances(audio,paused),"Logical resume must restore audio rendering");
    check(audio.set_suspended(true),"Second device suspension failed");audio.configure(lm::Json::object());std::this_thread::sleep_for(std::chrono::milliseconds(120));check(frames(audio)==0,"Mixer reconfiguration must not restart a suspended device");
    check(audio.set_suspended(false)&&advances(audio,0),"Reconfigured suspended mixer failed to resume");
    std::cout<<"PASS audio backend "<<state.at("device_backend")<<", physical suspend/resume, retained sample clock, logical pause, suspended reconfiguration\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
