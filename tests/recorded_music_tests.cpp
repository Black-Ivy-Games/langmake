#include "langmake/music.hpp"
#include "langmake/recorded_music.hpp"
#include "langmake/project.hpp"
#include "langmake/asset_runtime.hpp"
#include "langmake/world.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
namespace { bool realtime=false; int deletes=0; }
void* operator new(std::size_t n){if(realtime)throw std::bad_alloc();if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{if(realtime&&p)++deletes;std::free(p);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t)noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t)noexcept{::operator delete(p);}
namespace {
using lm::Json;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
template<class F>void rejects(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("Expected rejection");}
std::string wav(int frames=4800,int rate=48000,int sample=8192){
    std::string s;auto u16=[&](unsigned v){s+=char(v&255);s+=char(v>>8);};auto u32=[&](unsigned v){u16(v&65535);u16(v>>16);};
    s+="RIFF";u32(36+frames*2);s+="WAVEfmt ";u32(16);u16(1);u16(1);u32(rate);u32(rate*2);u16(2);u16(16);s+="data";u32(frames*2);for(int i=0;i<frames;++i)u16(sample);return s;
}
lm::AssetSource fixture(){lm::AssetSource s;s.type="music";s.path="recorded.json";s.document={{"schema_version",2},{"type","music"},{"length_frames",4800},{"gain",1},{"beat_frames",{0,1200,2400,3600}},{"beats_per_bar",2},{"tracks",Json::array({{{"id","pad"},{"source","pad.wav"}}})}};s.inputs["pad.wav"]=wav();return s;}
std::vector<float> render(lm::MusicVoice& voice,int frames,int block){std::vector<float> out(frames*2);for(int n=0;n<frames;n+=block)voice.render(out.data()+n*2,std::min(block,frames-n));return out;}
std::string bytes(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void recorded(){
    auto f=fixture();auto score=lm::compile_music(f);check(lm::inspect_music(*score)["decoded_bytes"]==19200,"PCM budget excludes decoded channels");
    lm::MusicVoice a(score),b(score);check(render(a,12000,1)==render(b,12000,511),"recorded loop changes with block size");
    a.reset();auto pcm=render(a,1,1);check(std::abs(pcm[0]-.25f)<.001f&&pcm[0]==pcm[1],"mono decode/signed PCM incorrect");
    auto bad=f;bad.document["tracks"][0]["source"]="../secret.wav";rejects([&]{lm::compile_music(bad);});
    bad=f;bad.inputs["pad.wav"]="bad";rejects([&]{lm::compile_music(bad);});
    bad=f;bad.document["tracks"].push_back(bad.document["tracks"][0]);rejects([&]{lm::compile_music(bad);});
    bad=f;bad.document["beat_frames"]={0,50,40};rejects([&]{lm::compile_music(bad);});
    bad=f;bad.document["tracks"][0]["automation"]=Json::array({{{"frame",1},{"gain",0}}});rejects([&]{lm::compile_music(bad);});
    bad=f;bad.document["tracks"][0]["effects"]=Json::array({{{"type","plugin"}}});rejects([&]{lm::compile_music(bad);});
    f.document["tracks"][0]["automation"]=Json::array({{{"frame",0},{"gain",0},{"pan",-1}},{{"frame",2400},{"gain",1},{"pan",1}}});
    lm::MusicVoice automation(lm::compile_music(f));pcm=render(automation,2401,255);
    check(pcm[0]==0&&pcm[4800]==0&&std::abs(pcm[4801]-.25f)<.001,"gain/pan automation endpoint mismatch");
    f=fixture();f.document["length_frames"]=4800;f.inputs["pad.wav"]=wav(4410,44100);lm::MusicVoice resampled(lm::compile_music(f));pcm=render(resampled,4800,511);check(std::abs(pcm[4000]-.25f)<.01,"sample-rate conversion failed");
}
void layers(){
    auto score=lm::compile_music(fixture());lm::MusicVoice v(score);v.render(nullptr,0);render(v,13,13);
    check(v.set_layer("pad",0,0,1),"known layer rejected");auto pcm=render(v,1300,89);
    check(pcm[(1200-13-1)*2]>.24&&pcm[(1200-13)*2]==0,"beat transition fired at wrong sample");
    check(!v.set_layer("missing",1,0,0),"unknown layer accepted");
    v.reset();render(v,2401,77);v.set_layer("pad",0,0,2);pcm=render(v,2500,256);
    check(pcm[(4800-2401-1)*2]>.24&&pcm[(4800-2401)*2]==0,"bar transition across loop failed");
    v.reset();v.render(nullptr,0);v.set_layer("pad",0,.01f,0);pcm=render(v,481,480);
    check(std::abs(pcm[0]-.25)<.001&&std::abs(pcm[480]-.125)<.001&&pcm[960]==0,"layer fade duration incorrect");
    lm::GameAudioMixer mixer;mixer.set_music(score);mixer.set_music_layer("pad",.5f,.01f,"beat");std::array<float,1024> buffer{};
    realtime=true;mixer.render(buffer.data(),512);mixer.render(buffer.data(),512);realtime=false;
    check(deletes==0,"callback deleted prepared resources");
    check(mixer.measurements()["dropped_commands"]==0,"valid layer command dropped");
}
void dynamics(){
    lm::AudioEffects limiter(Json::array({{{"type","limiter"},{"ceiling_db",-6}}}));float l=4,r=-2;limiter.process(l,r);check(l<=.502&&std::abs(r/l+.5)<.001,"linked peak limiter failed");
    lm::AudioEffects compressor(Json::array({{{"type","compressor"},{"threshold_db",-12},{"ratio",4},{"attack_ms",.1}}}));
    for(int i=0;i<48000;++i){l=1;r=.5;compressor.process(l,r);}check(l<.37&&l>.34&&std::abs(r/l-.5)<.001,"compression gain/stereo linking failed");
    lm::AudioEffects eq(Json::array({{{"type","eq"},{"frequency_hz",1000},{"gain_db",6},{"q",1}}}));double energy=0,dry=0;
    for(int i=0;i<4800;++i){float x=float(std::sin(i*6.283185307179586*1000/48000));l=r=x;eq.process(l,r);if(i>2400){energy+=l*l;dry+=x*x;}}
    check(std::abs(std::sqrt(energy/dry)-std::pow(10.,6./20))<.01,"parametric EQ center gain failed");
}
void integration(const std::filesystem::path& root){
    const auto directory=root/".cache/recorded-audio-tests";std::filesystem::create_directories(directory);
    std::ofstream(directory/"pad.wav",std::ios::binary)<<wav();std::ofstream(directory/"music.json")<<fixture().document.dump();
    Json project{{"schema_version",2},{"title","Recorded audio test"},{"dimension","2d"},{"script","main.luau"},{"music","theme"},{"entities",Json::array()},
        {"assets",{{"theme",{{"type","music"},{"path","music.json"}}}}}};
    std::ofstream(directory/"project.json")<<project.dump();
    std::ofstream(directory/"main.luau")<<"return {start=function() engine.set_music_layer('pad',0.5,0.1,'bar') end}";
    auto loaded=lm::load_project(directory);auto revision=loaded.revision;
    check(loaded.sources.contains("pad.wav"),"recorded dependency missing from source graph");
    lm::World world(loaded);check(world.snapshot()["music_layers"]["pad"]["gain"]==.5,"layer target not observable");
    std::ofstream(directory/"pad.wav",std::ios::binary)<<wav(4800,48000,4096);check(lm::load_project(directory).revision!=revision,"recorded source revision did not change");
    auto saved=world.save();world.restore(saved);check(world.snapshot()["music_layers"]["pad"]["gain"]==.5,"layer target lost on save/load");
    auto f=fixture();f.inputs["pad.wav"]=bytes(root/".deps/source/miniaudio/data/16-44100-stereo.flac");f.document["tracks"][0]["source"]="pad.flac";f.inputs["pad.flac"]=f.inputs["pad.wav"];
    lm::MusicVoice flac(lm::compile_music(f));auto pcm=render(flac,4800,512);check(std::any_of(pcm.begin(),pcm.end(),[](float x){return std::abs(x)>.0001;}),"FLAC decode produced silence");
    // Run the same tick-stamped layer/SFX events through the callback mixer and
    // offline exporter, using a different callback block size at every tick.
    f=fixture();f.document["effects"]=Json::array({{{"type","eq"},{"frequency_hz",700},{"gain_db",3}},{{"type","compressor"},{"threshold_db",-18}},{{"type","limiter"},{"ceiling_db",-2}}});
    const auto music=lm::compile_music(f);const Json config={{"tail_seconds",0}};
    const Json events=Json::array({{{"tick",0},{"action","music"},{"asset","recorded"}},
        {{"tick",1},{"action","layer"},{"layer","pad"},{"gain",0},{"fade_seconds",.01},{"quantization","bar"}},
        {{"tick",4},{"action","sound"},{"asset","recorded"}},
        {{"tick",7},{"action","layer"},{"layer","pad"},{"gain",.8},{"fade_seconds",.02},{"quantization","beat"}}});
    const auto file=directory/"recorded-events.wav";
    const auto report=lm::render_game_audio_wav({{"recorded",music}},events,12,file,config);
    lm::GameAudioMixer live(config);std::vector<float> expected;
    for(int tick=0;tick<12;++tick){
        if(tick==0)live.set_music(music);
        if(tick==1)live.set_music_layer("pad",0,.01f,"bar");
        if(tick==4)live.play_sfx(music);
        if(tick==7)live.set_music_layer("pad",.8f,.02f,"beat");
        std::array<float,1600> chunk{};
        for(int pos=0;pos<800;){auto count=std::min(800-pos,17+tick*13);live.render(chunk.data()+pos*2,count);pos+=count;}
        expected.insert(expected.end(),chunk.begin(),chunk.end());
    }
    const auto output=bytes(file);check(output.size()==44+expected.size()*2,"recorded event WAV frame count");
    for(std::size_t i=0;i<expected.size();++i){auto sample=uint16_t(uint8_t(output[44+i*2]))|(uint16_t(uint8_t(output[45+i*2]))<<8);check(sample==uint16_t(int16_t(std::lrint(expected[i]*32767))),"recorded callback/offline PCM differs");}
    check(report["clipped_frames"]==0&&report["dropped_commands"]==0,"recorded events clipped or dropped");
}
}
int main(int argc,char**argv){try{recorded();layers();dynamics();integration(argc>1?std::filesystem::path(argv[1]):std::filesystem::current_path());std::cout<<"PASS recorded PCM, resampling/FLAC, strict validation, timing/loops, layers/fades, realtime allocation, EQ/compression/limiting, source revision and saves\n";return 0;}catch(const std::exception& e){realtime=false;std::cerr<<e.what()<<'\n';return 1;}}
