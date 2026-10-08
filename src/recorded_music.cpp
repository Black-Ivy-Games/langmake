#include "langmake/recorded_music.hpp"
#include <miniaudio.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
// This exported ABI marker lets portable exporters reject older Android players.
#if defined(__GNUC__)
extern "C" __attribute__((visibility("default"),used)) int langmake_recorded_audio_v1(){return 1;}
#endif
namespace lm {
namespace {
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("Recorded music: "+message); }
void keys(const Json& j,std::initializer_list<const char*> allowed) {
    if(!j.is_object())fail("expected an object");
    for(auto it=j.begin();it!=j.end();++it)if(std::none_of(allowed.begin(),allowed.end(),[&](auto k){return it.key()==k;}))fail("unknown field: "+it.key());
}
double num(const Json& j,const char* key,double fallback,double lo,double hi) {
    if(!j.contains(key))return fallback;
    if(!j[key].is_number())fail(std::string(key)+" must be numeric");
    double v=j[key].get<double>();if(!std::isfinite(v)||v<lo||v>hi)fail(std::string(key)+" outside range");return v;
}
uint64_t integer(const Json& j,const char* key,uint64_t fallback,uint64_t lo,uint64_t hi) {
    if(!j.contains(key))return fallback;
    if(!j[key].is_number_integer()||j[key]<lo||j[key]>hi)fail(std::string(key)+" must be a bounded integer");return j[key].get<uint64_t>();
}
std::string source_path(const Json& track) {
    if(!track.contains("source")||!track["source"].is_string())fail("track requires source");
    auto p=track["source"].get<std::string>();
    if(p.empty()||p.size()>512||p.find('\\')!=std::string::npos||p.find(':')!=std::string::npos||p.front()=='/')fail("source must be a portable project-relative WAV/FLAC path");
    for(const auto& c:std::filesystem::path(p))if(c==".."||c==".")fail("source cannot contain . or ..");
    auto ext=std::filesystem::path(p).extension();if(ext!=".wav"&&ext!=".flac")fail("source extension must be .wav or .flac");return p;
}
}
std::vector<std::string> music_dependencies(const AssetSource& source) {
    auto d=source.document.is_null()?Json::parse(source.bytes):source.document;
    std::vector<std::string> result;
    if(d.value("schema_version",Json(1))==2&&d.contains("tracks")) {
        if(!d["tracks"].is_array()||d["tracks"].size()>16)fail("tracks must contain at most 16 entries");
        for(auto& t:d["tracks"])result.push_back(source_path(t));
    }
    return result;
}
std::shared_ptr<const RecordedMusic> compile_recorded_music(const AssetSource& source) {
    const auto d=source.document.is_null()?Json::parse(source.bytes):source.document;
    keys(d,{"schema_version","type","title","bpm","gain","length_frames","beats_per_bar","beat_frames","tracks","effects","tail_seconds","provenance"});
    auto out=std::make_shared<RecordedMusic>();
    out->length=integer(d,"length_frames",0,1,48000*600);if(!out->length)fail("length_frames is required");
    out->beats_per_bar=int(integer(d,"beats_per_bar",4,1,16));
    if(!d.contains("tracks")||!d["tracks"].is_array()||d["tracks"].empty()||d["tracks"].size()>16)fail("expected 1..16 tracks");
    std::set<std::string> ids;
    for(const auto& t:d["tracks"]) {
        keys(t,{"id","source","offset_frames","gain","pan","automation","effects"});RecordedMusic::Track track;
        if(!t.contains("id")||!t["id"].is_string())fail("track requires ID");track.id=t["id"].get<std::string>();
        if(track.id.empty()||track.id.size()>64||track.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||!ids.insert(track.id).second)fail("invalid or duplicate track ID");
        track.source=source_path(t);track.offset=integer(t,"offset_frames",0,0,out->length-1);
        track.gain=float(num(t,"gain",1,0,2));track.pan=float(num(t,"pan",0,-1,1));
        track.effects=t.value("effects",Json::array());AudioEffects check(track.effects);
        if(t.contains("automation")) {
            if(!t["automation"].is_array()||t["automation"].size()>4096)fail("automation limit is 4096 points per track");
            for(auto& p:t["automation"]) {
                keys(p,{"frame","gain","pan"});auto frame=integer(p,"frame",out->length,0,out->length-1);
                if(frame>=out->length||(!track.automation.empty()&&frame<=track.automation.back().frame))fail("automation frames must be present, increasing and inside length");
                track.automation.push_back({frame,float(num(p,"gain",1,0,2)),float(num(p,"pan",track.pan,-1,1))});
            }
            if(!track.automation.empty()&&track.automation.front().frame!=0)fail("automation must start at frame zero");
        }
        auto input=source.inputs.find(track.source);if(input==source.inputs.end())fail("missing registered audio source: "+track.source);
        // Probe before allocation. Output is always 48 kHz stereo signed PCM16.
        auto config=ma_decoder_config_init(ma_format_s16,2,48000);ma_decoder decoder{};
        auto code=ma_decoder_init_memory(input->second.data(),input->second.size(),&config,&decoder);
        if(code!=MA_SUCCESS)fail("cannot decode "+track.source+": "+ma_result_description(code));
        struct Guard {ma_decoder* d;~Guard(){ma_decoder_uninit(d);}} guard{&decoder};
        ma_uint64 frames=0;code=ma_decoder_get_length_in_pcm_frames(&decoder,&frames);
        if(code!=MA_SUCCESS||!frames||frames>48000*600||frames*4>96*1024*1024-out->bytes)fail("decoded PCM exceeds 96 MiB per music asset or 600 seconds");
        track.pcm.resize(std::size_t(frames)*2);ma_uint64 actual=0;
        code=ma_decoder_read_pcm_frames(&decoder,track.pcm.data(),frames,&actual);
        if((code!=MA_SUCCESS&&code!=MA_AT_END)||actual!=frames)fail("truncated audio: "+track.source);
        out->bytes+=frames*4;out->tracks.push_back(std::move(track));
    }
    if(d.contains("beat_frames")) {
        if(!d["beat_frames"].is_array()||d["beat_frames"].empty()||d["beat_frames"].size()>20000)fail("beat_frames requires 1..20000 frame offsets");
        for(auto& b:d["beat_frames"]) {
            if(!b.is_number_integer()||b<0||b>=out->length)fail("beat frame outside length");auto f=b.get<uint64_t>();
            if(!out->beats.empty()&&f<=out->beats.back())fail("beat frames must increase");out->beats.push_back(f);
        }
        if(out->beats.front()!=0)fail("beat_frames must start at zero");
    } else {
        const auto bpm=num(d,"bpm",120,20,400);
        for(uint64_t b=0;;++b){auto f=uint64_t(std::llround(b*60.*48000/bpm));if(f>=out->length)break;out->beats.push_back(f);}
    }
    return out;
}
RecordedVoice::RecordedVoice(std::shared_ptr<const RecordedMusic> score):score_(std::move(score)) {
    for(auto& t:score_->tracks)effects_.push_back(std::make_unique<AudioEffects>(t.effects));reset();
}
void RecordedVoice::reset(){layers_={};for(auto& e:effects_)e->reset();}
bool RecordedVoice::set_layer(std::string_view id,float gain,float seconds,int quantization,uint64_t frame,bool loop) {
    for(std::size_t i=0;i<score_->tracks.size();++i)if(score_->tracks[i].id==id) {
        uint64_t start=frame;
        if(quantization) {
            const auto cycle=frame/score_->length, local=frame%score_->length;bool found=false;
            for(std::size_t b=0;b<score_->beats.size();++b)if((quantization==1||b%score_->beats_per_bar==0)&&score_->beats[b]>=local){start=cycle*score_->length+score_->beats[b];found=true;break;}
            if(!found)start=loop?(cycle+1)*score_->length:score_->length;
        }
        auto& l=layers_[i];l.target=gain;l.start=start;l.duration=uint64_t(std::llround(seconds*48000));l.pending=true;return true;
    }
    return false;
}
void RecordedVoice::sample(float& left,float& right,uint64_t frame,bool loop) {
    const auto local=loop?frame%score_->length:frame;
    left=right=0;
    for(std::size_t i=0;i<score_->tracks.size();++i) {
        const auto& t=score_->tracks[i];auto& l=layers_[i];
        if(l.pending&&frame>=l.start){l.from=l.gain;l.pending=false;}
        if(!l.pending&&frame>=l.start)l.gain=l.duration?l.from+(l.target-l.from)*float(std::min(1.,double(frame-l.start)/l.duration)):l.target;
        float gain=1,pan=t.pan;
        if(local==0)l.point=0;
        if(!t.automation.empty()) {
            while(l.point+1<t.automation.size()&&t.automation[l.point+1].frame<=local)++l.point;
            auto a=t.automation[l.point];gain=a.gain;pan=a.pan;
            if(l.point+1<t.automation.size()) {auto b=t.automation[l.point+1];float f=float(double(local-a.frame)/(b.frame-a.frame));gain+=(b.gain-a.gain)*f;pan+=(b.pan-a.pan)*f;}
        }
        float a=0,b=0;
        if(local<score_->length&&local>=t.offset&&(local-t.offset)*2+1<t.pcm.size()) {a=t.pcm[(local-t.offset)*2]/32768.f;b=t.pcm[(local-t.offset)*2+1]/32768.f;}
        effects_[i]->process(a,b);gain*=t.gain*l.gain;
        left+=a*gain*(pan>0?1-pan:1);right+=b*gain*(pan<0?1+pan:1);
    }
}
Json recorded_music_schema() {
    auto n=[](double lo,double hi){return Json{{"type","number"},{"minimum",lo},{"maximum",hi}};};
    auto f=Json{{"type","integer"},{"minimum",0},{"maximum",48000*600-1}};
    Json point{{"type","object"},{"additionalProperties",false},{"required",{"frame"}},{"properties",{{"frame",f},{"gain",n(0,2)},{"pan",n(-1,1)}}}};
    Json track{{"type","object"},{"additionalProperties",false},{"required",{"id","source"}},{"properties",{
        {"id",{{"type","string"},{"pattern","^[A-Za-z0-9_.-]{1,64}$"}}},{"source",{{"type","string"},{"pattern","\\.(wav|flac)$"}}},
        {"offset_frames",f},{"gain",n(0,2)},{"pan",n(-1,1)},{"effects",audio_effects_schema()},
        {"automation",{{"type","array"},{"maxItems",4096},{"items",point}}}}}};
    return {{"type","object"},{"additionalProperties",false},{"required",{"schema_version","type","length_frames","tracks"}},{"properties",{
        {"schema_version",{{"const",2}}},{"type",{{"const","music"}}},{"title",{{"type","string"},{"minLength",1},{"maxLength",128}}},
        {"bpm",n(20,400)},{"gain",n(0,1)},{"length_frames",{{"type","integer"},{"minimum",1},{"maximum",48000*600}}},
        {"beats_per_bar",{{"type","integer"},{"minimum",1},{"maximum",16}}},{"beat_frames",{{"type","array"},{"minItems",1},{"maxItems",20000},{"items",f}}},
        {"tracks",{{"type","array"},{"minItems",1},{"maxItems",16},{"items",track}}},{"effects",audio_effects_schema()},
        {"tail_seconds",n(0,10)},{"provenance",{{"type","object"}}}}},
        {"$comment","48 kHz output; WAV/FLAC dependencies are project-relative; decoded PCM budget 96 MiB per asset, 192 MiB per project. Gain automation is multiplicative; pan automation is absolute. Beat frames start at zero and increase. End-of-file pads silence; length_frames defines the exact loop boundary."}};
}
}
