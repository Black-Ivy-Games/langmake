#include "langmake/audio_mixer.hpp"
#include "langmake/music.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {std::atomic<bool> forbid_allocations{false};std::atomic<unsigned> callback_deletes{0};}
#if defined(__GNUC__)
#define LM_NOINLINE __attribute__((noinline))
#else
#define LM_NOINLINE
#endif
LM_NOINLINE void* operator new(std::size_t size){if(forbid_allocations.load())throw std::bad_alloc();if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
LM_NOINLINE void* operator new[](std::size_t size){return ::operator new(size);}
LM_NOINLINE void operator delete(void* pointer)noexcept{if(forbid_allocations.load()&&pointer)++callback_deletes;std::free(pointer);}
LM_NOINLINE void operator delete[](void* pointer)noexcept{::operator delete(pointer);}
LM_NOINLINE void operator delete(void* pointer,std::size_t)noexcept{::operator delete(pointer);}
LM_NOINLINE void operator delete[](void* pointer,std::size_t)noexcept{::operator delete(pointer);}
namespace {
using lm::Json;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action){try{action();}catch(const std::runtime_error&){return;}throw std::runtime_error("Invalid audio configuration accepted");}
std::shared_ptr<const lm::MusicScore> score(Json effects=Json::array()){
    lm::AssetSource source;source.type="music";source.path="audio-test.music.json";source.revision="audio-test";
    source.document={{"schema_version",1},{"type","music"},{"bpm",120},{"ppq",96},{"length",96},{"gain",.5},
        {"instruments",{{"tone",{{"wave","sine"},{"gain",.2},{"attack",0},{"decay",0},{"sustain",1},{"release",0}}}}},
        {"notes",Json::array({{{"tick",0},{"duration",48},{"note",69},{"instrument","tone"}}})},{"effects",effects}};
    return lm::compile_music(source);
}
std::vector<float> render(lm::GameAudioMixer& mixer,std::size_t frames,std::size_t block){
    std::vector<float> result(frames*2);for(std::size_t i=0;i<frames;i+=block)mixer.render(result.data()+i*2,std::min(block,frames-i));return result;
}
void effects(){
    lm::AudioEffects delay(Json::array({{{"type","delay"},{"seconds",.01},{"feedback",.5},{"wet",1}}}));
    std::vector<float> impulse(1441);for(std::size_t i=0;i<impulse.size();++i){float l=i==0?1:0,r=l;delay.process(l,r);impulse[i]=l;check(l==r,"Delay stereo asymmetry");}
    check(impulse[0]==0 && impulse[480]==1 && impulse[960]==.5f && impulse[1440]==.25f,"Delay impulse timing or feedback incorrect");
    delay.reset();float l=0,r=0;delay.process(l,r);check(l==0&&r==0,"Reset must erase effect history");
    lm::AudioEffects low(Json::array({{{"type","lowpass"},{"cutoff_hz",200}}}));
    lm::AudioEffects high(Json::array({{{"type","highpass"},{"cutoff_hz",200}}}));
    double low_energy=0,high_energy=0;
    for(int i=0;i<4000;++i){float a=(i%2)?-1:1,b=a,c=a,d=a;low.process(a,b);high.process(c,d);if(i>1000){low_energy+=a*a;high_energy+=c*c;}}
    check(low_energy<high_energy*.001,"Lowpass must suppress high frequencies");
    lm::AudioEffects reverb(Json::array({{{"type","reverb"},{"wet",1}}}));bool sound=false;
    for(int i=0;i<10000;++i){float a=i==0?1:0,b=a;reverb.process(a,b);if(i>1000 && (a!=0||b!=0))sound=true;check(std::isfinite(a)&&std::isfinite(b),"Reverb unstable");}
    check(sound,"Reverb must generate an audible tail");
    rejects([]{lm::AudioEffects bad(Json::array({{{"type","delay"},{"feedback",1}}}));});
    rejects([]{lm::AudioEffects bad(Json::array({{{"type","lowpass"},{"typo",1}}}));});
    Json buses=Json::object();for(int i=0;i<6;++i)buses["b"+std::to_string(i)]=Json::object();
    rejects([&]{lm::validate_audio_mixer({{"buses",buses}});});
    check(lm::audio_effects_schema()["maxItems"]==4,"Effects schema limit missing");
}
void dry_and_blocks(){
    auto music=score();lm::MusicVoice dry(music,false);std::vector<float> expected(48000);dry.render(expected.data(),24000);
    lm::GameAudioMixer mixer;mixer.set_music(music,false);check(render(mixer,24000,257)==expected,"Dry mixer must preserve score PCM");
    const Json config={{"buses",{{"sfx",{{"effects",Json::array({{{"type","delay"},{"seconds",.017},{"wet",.3},{"feedback",.4}},{{"type","reverb"},{"wet",.2}}})}}}}}};
    lm::GameAudioMixer a(config),b(config);a.play_sfx(music);a.play_sfx(music);b.play_sfx(music);b.play_sfx(music);
    check(render(a,50000,1)==render(b,50000,1024),"Summed bus DSP must be block-size independent");
    auto shaped=score(Json::array({{{"type","delay"},{"seconds",.1},{"wet",.4}}}));
    check(lm::inspect_music(*shaped)["sample_frames"].get<int>()>24000,"Asset effects need finite rendered tail");
    lm::MusicVoice shaped_voice(shaped,false);shaped_voice.render(nullptr,0);
    std::array<float,1024> buffer{};{forbid_allocations=true;shaped_voice.render(buffer.data(),512);shaped_voice.reset();shaped_voice.render(buffer.data(),512);forbid_allocations=false;}
    check(callback_deletes==0,"Voice reset/render must not free memory");
}
void controls_and_realtime(){
    const auto music=score();lm::GameAudioMixer baseline,controlled;baseline.set_music(music);controlled.set_music(music);
    const auto original=render(baseline,2000,137);controlled.set_bus("music",1,1);auto panned=render(controlled,2000,137);
    check(std::all_of(panned.begin()+1000,panned.end(),[](float x){return std::isfinite(x);}),"Pan produced nonfinite samples");
    for(int i=480;i<2000;++i)check(panned[2*i]==0 && panned[2*i+1]==original[2*i+1],"Pan must reach target in 10 ms");
    controlled.set_bus("master",1,0,true);auto muted=render(controlled,800,800);check(std::all_of(muted.begin()+960,muted.end(),[](float x){return x==0;}),"Mute smoothing must settle");
    lm::GameAudioMixer paused,reference;paused.set_music(music);reference.set_music(music);paused.set_paused(true);
    check(render(paused,1024,256)==std::vector<float>(2048),"Pause must output silence");paused.set_paused(false);check(render(paused,1024,256)==render(reference,1024,13),"Pause must preserve sample clock");
    const Json configuration={{"buses",{{"sfx",{{"effects",Json::array({{{"type","delay"},{"wet",.5}}})}}}}}};
    lm::GameAudioMixer realtime(configuration);realtime.play_sfx(music);realtime.play_sfx(music);std::array<float,2048> output{};
    {forbid_allocations=true;for(int i=0;i<40;++i)realtime.render(output.data(),1024);forbid_allocations=false;}
    check(callback_deletes==0,"Realtime callback must not destroy voices or effect buffers");
    for(int i=0;i<200;++i)realtime.set_bus("master",1,0);check(realtime.dropped_commands()>0,"Queue overload must be bounded and observable");
    rejects([&]{realtime.set_bus("absent",1);});rejects([&]{realtime.set_bus("music",3);});
}
void offline(const std::filesystem::path& root){
    const auto music=score();const auto directory=root/".cache/audio-mixer-tests";const auto file=directory/"scenario.wav";
    const Json config={{"tail_seconds",0},{"buses",{{"music",{{"effects",Json::array({{{"type","lowpass"},{"cutoff_hz",700}}})}}}}}};
    const Json events=Json::array({{{"tick",0},{"action","music"},{"asset","tone"},{"loop",false}},
        {{"tick",3},{"action","sound"},{"asset","tone"}},{{"tick",5},{"action","bus"},{"bus","master"},{"gain",.4},{"pan",-.2}}});
    const auto report=lm::render_game_audio_wav({{"tone",music}},events,12,file,config);
    lm::GameAudioMixer live(config);std::vector<float> pcm;for(int tick=0;tick<12;++tick){if(tick==0)live.set_music(music,false);if(tick==3)live.play_sfx(music);if(tick==5)live.set_bus("master",.4f,-.2f);auto block=render(live,800,137);pcm.insert(pcm.end(),block.begin(),block.end());}
    std::ifstream input(file,std::ios::binary);std::vector<unsigned char> bytes(std::istreambuf_iterator<char>(input),{});
    check(bytes.size()==44+pcm.size()*2,"Offline WAV frame count incorrect");
    for(std::size_t i=0;i<pcm.size();++i){const auto actual=uint16_t(bytes[44+i*2])|(uint16_t(bytes[45+i*2])<<8);const auto expected=uint16_t(int16_t(std::lrint(pcm[i]*32767)));check(actual==expected,"Live block rendering and tick-event WAV differ");}
    check(report["clipped_frames"]==0 && report["sample_frames"]==9600,"Offline measurements incorrect");
    auto transition=events;transition.push_back({{"tick",8},{"action","configure"},{"configuration",{{"buses",{{"music",{{"gain",.25}}}}},{"tail_seconds",0}}},{"music","second::tone"}});
    const auto switched=lm::render_game_audio_wav({{"tone",music},{"second::tone",music}},transition,12,directory/"transition.wav",config);
    check(switched["sample_frames"]==9600 && switched["peak"].get<double>()>0 && switched["rms"].get<double>()>0,"Scene mixer replacement must retain aggregate measurements");
    rejects([&]{lm::render_game_audio_wav({{"tone",music}},Json::array({{{"tick",2},{"action","stop_music"}},{{"tick",1},{"action","stop_music"}}}),3,directory/"invalid.wav");});
}
}
int main(int argc,char** argv){try{effects();dry_and_blocks();controls_and_realtime();offline(argc>1?std::filesystem::path(argv[1]):std::filesystem::current_path());std::cout<<"PASS effects, dry compatibility, block invariance, bus smoothing, pause, realtime allocation limits, offline/live PCM equality\n";return 0;}catch(const std::exception& e){forbid_allocations=false;std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
