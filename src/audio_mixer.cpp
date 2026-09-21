#include "langmake/audio_mixer.hpp"
#include "langmake/music.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace lm {
namespace {
constexpr double tau = 6.2831853071795864769;
void object(const Json& value, std::initializer_list<const char*> keys) {
    if (!value.is_object()) throw std::runtime_error("Audio configuration must be an object");
    for (auto it=value.begin(); it!=value.end(); ++it)
        if (std::none_of(keys.begin(),keys.end(),[&](const char* key){return it.key()==key;}))
            throw std::runtime_error("Unknown audio field: " + it.key());
}
double number(const Json& value, const char* key, double fallback, double low, double high) {
    if (!value.contains(key)) return fallback;
    if (!value[key].is_number()) throw std::runtime_error(std::string("Audio ")+key+" must be a number");
    const double result=value[key].get<double>();
    if (!std::isfinite(result) || result<low || result>high) throw std::runtime_error(std::string("Audio ")+key+" is outside its supported range");
    return result;
}
bool boolean(const Json& value,const char* key,bool fallback=false) {
    if (!value.contains(key)) return fallback;
    if (!value[key].is_boolean()) throw std::runtime_error(std::string("Audio ")+key+" must be boolean");
    return value[key].get<bool>();
}
struct Effect {
    enum class Kind { lowpass, highpass, delay, reverb } kind;
    float wet=1, feedback=0, coefficient=0, damping=0;
    std::array<float,2> filter{};
    std::vector<float> delay;
    std::size_t cursor=0;
    struct Comb { std::vector<float> samples; std::size_t cursor=0; float damped=0; };
    std::array<Comb,8> combs;
    double tail=0;
    void process(float& left,float& right) {
        if (kind==Kind::lowpass || kind==Kind::highpass) {
            float* channels[]{&left,&right};
            for(int i=0;i<2;++i) {
                filter[i]+=coefficient*(*channels[i]-filter[i]);
                *channels[i]=kind==Kind::lowpass ? filter[i] : *channels[i]-filter[i];
            }
        } else if (kind==Kind::delay) {
            const float delayed_left=delay[cursor], delayed_right=delay[cursor+1];
            delay[cursor]=left+delayed_left*feedback; delay[cursor+1]=right+delayed_right*feedback;
            left=left*(1-wet)+delayed_left*wet; right=right*(1-wet)+delayed_right*wet;
            cursor=(cursor+2)%delay.size();
        } else {
            float reverberated[2]{};
            for(int i=0;i<8;++i) {
                auto& comb=combs[i]; const float delayed=comb.samples[comb.cursor];
                comb.damped=delayed*(1-damping)+comb.damped*damping;
                comb.samples[comb.cursor]=(i<4 ? left:right)+comb.damped*feedback;
                comb.cursor=(comb.cursor+1)%comb.samples.size();
                reverberated[i/4]+=delayed*.25f;
            }
            left=left*(1-wet)+reverberated[0]*wet; right=right*(1-wet)+reverberated[1]*wet;
        }
    }
    void reset() {
        filter={}; cursor=0; std::fill(delay.begin(),delay.end(),0.f);
        for(auto& comb:combs) { comb.cursor=0; comb.damped=0; std::fill(comb.samples.begin(),comb.samples.end(),0.f); }
    }
};
std::vector<Effect> compile_effects(const Json& list,int rate) {
    if(!list.is_array() || list.size()>4) throw std::runtime_error("Audio effects must contain at most four effects");
    std::vector<Effect> result;
    for(const auto& item:list) {
        if(!item.is_object() || !item.contains("type") || !item["type"].is_string()) throw std::runtime_error("Audio effect requires type");
        const auto type=item["type"].get<std::string>(); Effect effect{};
        if(type=="lowpass" || type=="highpass") {
            object(item,{"type","cutoff_hz"});
            effect.kind=type=="lowpass" ? Effect::Kind::lowpass:Effect::Kind::highpass;
            const double cutoff=number(item,"cutoff_hz",1000,20,20000);
            effect.coefficient=float(1-std::exp(-tau*std::min(cutoff,rate*.45)/rate));
            effect.tail=.1;
        } else if(type=="delay") {
            object(item,{"type","seconds","feedback","wet"}); effect.kind=Effect::Kind::delay;
            const double seconds=number(item,"seconds",.2,.001,2);
            effect.feedback=float(number(item,"feedback",.3,0,.95)); effect.wet=float(number(item,"wet",.25,0,1));
            effect.delay.resize(std::max<std::size_t>(1,std::size_t(std::llround(seconds*rate)))*2);
            effect.tail=effect.wet==0 ? 0 : seconds*(effect.feedback==0 ? 1:std::ceil(std::log(.0001)/std::log(effect.feedback)));
        } else if(type=="reverb") {
            object(item,{"type","room_size","damping","wet"}); effect.kind=Effect::Kind::reverb;
            effect.feedback=float(.3+.65*number(item,"room_size",.5,0,1));
            effect.damping=float(number(item,"damping",.5,0,.99)); effect.wet=float(number(item,"wet",.2,0,1));
            constexpr double lengths[]{.0297,.0371,.0411,.0437,.0307,.0383,.0423,.0449};
            for(int i=0;i<8;++i) effect.combs[i].samples.resize(std::max<std::size_t>(1,std::size_t(std::llround(lengths[i]*rate))));
            effect.tail=effect.wet==0 ? 0 : .05*std::ceil(std::log(.0001)/std::log(effect.feedback));
        } else throw std::runtime_error("Unknown audio effect: "+type);
        result.push_back(std::move(effect));
    }
    return result;
}
Json range(double low,double high,double fallback) {return {{"type","number"},{"minimum",low},{"maximum",high},{"default",fallback}};}
}
struct AudioEffects::Impl { std::vector<Effect> effects; double tail=0; };
AudioEffects::AudioEffects(const Json& effects,int sample_rate):impl_(std::make_unique<Impl>()) {
    if(sample_rate<8000 || sample_rate>192000) throw std::runtime_error("Audio effect sample rate must be 8000..192000");
    impl_->effects=compile_effects(effects,sample_rate);
    for(const auto& effect:impl_->effects) impl_->tail+=effect.tail;
    impl_->tail=std::min(10.,impl_->tail);
}
AudioEffects::~AudioEffects()=default;
void AudioEffects::process(float& left,float& right) { for(auto& effect:impl_->effects) effect.process(left,right); }
void AudioEffects::reset() {for(auto& effect:impl_->effects) effect.reset();}
double AudioEffects::tail_seconds() const {return impl_->tail;}
Json audio_effects_schema() {
    auto shape=[](Json properties) {return Json{{"type","object"},{"additionalProperties",false},{"required",{"type"}},{"properties",std::move(properties)}};};
    return {{"type","array"},{"maxItems",4},{"items",{{"oneOf",Json::array({
        shape({{"type",{{"enum",{"lowpass","highpass"}}}},{"cutoff_hz",range(20,20000,1000)}}),
        shape({{"type",{{"const","delay"}}},{"seconds",range(.001,2,.2)},{"feedback",range(0,.95,.3)},{"wet",range(0,1,.25)}}),
        shape({{"type",{{"const","reverb"}}},{"room_size",range(0,1,.5)},{"damping",range(0,.99,.5)},{"wet",range(0,1,.2)}})})}}}};
}
Json audio_mixer_schema() {
    Json bus{{"type","object"},{"additionalProperties",false},{"properties",{{"gain",range(0,2,1)},{"pan",range(-1,1,0)},
        {"mute",{{"type","boolean"},{"default",false}}},{"effects",audio_effects_schema()}}}};
    return {{"type","object"},{"additionalProperties",false},{"properties",{{"tail_seconds",range(0,10,0)},
        {"buses",{{"type","object"},{"maxProperties",8},{"propertyNames",{{"pattern","^[A-Za-z0-9_-]{1,64}$"}}},{"additionalProperties",bus}}}}},
        {"$comment","master, music and sfx always exist and count toward the maximum of 8 buses. All non-master buses feed master. Controls ramp over 10 milliseconds."}};
}
Json validate_audio_mixer(const Json& configuration) {
    object(configuration,{"buses","tail_seconds"}); Json result=configuration;
    if(!result.contains("buses")) result["buses"]=Json::object();
    if(!result["buses"].is_object()) throw std::runtime_error("Audio buses must be an object");
    for(const char* id:{"master","music","sfx"}) if(!result["buses"].contains(id)) result["buses"][id]=Json::object();
    if(result["buses"].size()>8) throw std::runtime_error("Audio supports at most 8 buses including master/music/sfx");
    double tail=0;
    for(auto it=result["buses"].begin();it!=result["buses"].end();++it) {
        if(it.key().empty() || it.key().size()>64 || it.key().find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)
            throw std::runtime_error("Audio bus ID must contain 1..64 ASCII letters, digits, _ or -");
        auto& bus=it.value(); object(bus,{"gain","pan","mute","effects"});
        bus["gain"]=number(bus,"gain",1,0,2); bus["pan"]=number(bus,"pan",0,-1,1); bus["mute"]=boolean(bus,"mute");
        if(!bus.contains("effects")) bus["effects"]=Json::array();
        AudioEffects effects(bus["effects"]); if(it.key()!="master")tail=std::max(tail,effects.tail_seconds());
    }
    AudioEffects master(result["buses"]["master"]["effects"]);
    result["tail_seconds"]=number(configuration,"tail_seconds",std::min(10.,tail+master.tail_seconds()),0,10);
    return result;
}

struct GameAudioMixer::Impl {
    static constexpr int slot_count=32,sfx_count=8,queue_count=128;
    enum class Kind {tone,music,stop,stop_all,stop_sources,sfx,bus};
    enum class SlotState {free,prepared,active,retired};
    struct Slot {std::unique_ptr<MusicVoice> voice; std::atomic<SlotState> state{SlotState::free}; int bus=0;};
    struct Command {Kind kind=Kind::stop; int slot=-1,bus=0; float gain=1,pan=0,frequency=440,seconds=.13f; bool mute=false;};
    struct Bus {
        std::string id; std::unique_ptr<AudioEffects> effects;
        float gain=1,pan=0,target_gain=1,target_pan=0,gain_step=0,pan_step=0;
        int ramp=0;
        void controls(float next_gain,float next_pan,bool mute) {target_gain=mute?0:next_gain;target_pan=next_pan;ramp=480;gain_step=(target_gain-gain)/480;pan_step=(target_pan-pan)/480;}
        void process(float& left,float& right) {
            effects->process(left,right);
            if(ramp && --ramp==0) {gain=target_gain;pan=target_pan;}
            else if(ramp) {gain+=gain_step;pan+=pan_step;}
            left*=gain*(pan>0 ? 1-pan:1);right*=gain*(pan<0 ? 1+pan:1);
        }
    };
    std::array<Slot,slot_count> slots;
    std::array<Command,queue_count> commands{};
    std::atomic<unsigned> read{0},write{0}; std::atomic<bool> paused{false};
    std::atomic<std::uint64_t> dropped{0},published_frames{0},published_clipped{0};
    std::atomic<double> published_peak{0},published_rms{0};
    std::array<int,sfx_count> sfx;
    std::vector<Bus> buses;
    int master=0,music=-1,replace_effect=0,remaining=0,duration=1;
    double phase=0,frequency=440,peak=0,energy=0; std::uint64_t frames=0,clipped=0;
    explicit Impl(const Json& configuration) {
        sfx.fill(-1);const auto validated=validate_audio_mixer(configuration);
        for(auto it=validated["buses"].begin();it!=validated["buses"].end();++it) {
            Bus bus;bus.id=it.key();bus.effects=std::make_unique<AudioEffects>(it.value()["effects"]);
            bus.gain=bus.target_gain=it.value()["mute"].get<bool>()?0:it.value()["gain"].get<float>();
            bus.pan=bus.target_pan=it.value()["pan"].get<float>();
            if(bus.id=="master") master=int(buses.size());
            buses.push_back(std::move(bus));
        }
    }
    int bus_index(const std::string& id) const {for(std::size_t i=0;i<buses.size();++i) if(buses[i].id==id)return int(i);throw std::runtime_error("Unknown audio bus: "+id);}
    void reclaim() {for(auto& slot:slots)if(slot.state.load(std::memory_order_acquire)==SlotState::retired){slot.voice.reset();slot.state.store(SlotState::free,std::memory_order_release);}}
    bool push(Command command) {
        const auto tail=write.load(std::memory_order_relaxed),next=(tail+1)%queue_count;
        if(next==read.load(std::memory_order_acquire)){dropped.fetch_add(1,std::memory_order_relaxed);return false;}
        commands[tail]=command;write.store(next,std::memory_order_release);return true;
    }
    void submit(std::shared_ptr<const MusicScore> score,bool loop,Kind kind,const std::string& bus) {
        if(!score)throw std::runtime_error("Audio voice requires a compiled score");
        const auto index=bus_index(bus);reclaim();
        for(int i=0;i<slot_count;++i) {
            auto& slot=slots[i];if(slot.state.load(std::memory_order_acquire)!=SlotState::free)continue;
            slot.voice=std::make_unique<MusicVoice>(std::move(score),loop);slot.voice->render(nullptr,0,48000);slot.bus=index;
            slot.state.store(SlotState::prepared,std::memory_order_release);
            Command command;command.kind=kind;command.slot=i;
            if(!push(command)){slot.voice.reset();slot.state.store(SlotState::free,std::memory_order_release);}return;
        }
        dropped.fetch_add(1,std::memory_order_relaxed);
    }
    void retire(int index){if(index>=0)slots[index].state.store(SlotState::retired,std::memory_order_release);}
    void consume() {
        auto head=read.load(std::memory_order_relaxed);const auto tail=write.load(std::memory_order_acquire);
        while(head!=tail) {
            const auto command=commands[head];
            if(command.kind==Kind::tone){frequency=command.frequency;duration=std::max(1,int(command.seconds*48000));remaining=duration;phase=0;}
            else if(command.kind==Kind::stop || command.kind==Kind::stop_all || command.kind==Kind::stop_sources){retire(music);music=-1;if(command.kind!=Kind::stop){for(auto& effect:sfx){retire(effect);effect=-1;}remaining=0;if(command.kind==Kind::stop_all)for(auto& bus:buses)bus.effects->reset();}}
            else if(command.kind==Kind::bus)buses[command.bus].controls(command.gain,command.pan,command.mute);
            else if(command.kind==Kind::music){retire(music);music=command.slot;slots[music].state.store(SlotState::active,std::memory_order_release);}
            else {auto available=std::find(sfx.begin(),sfx.end(),-1);int index=available==sfx.end()?replace_effect:int(available-sfx.begin());if(available==sfx.end())replace_effect=(replace_effect+1)%sfx_count;retire(sfx[index]);sfx[index]=command.slot;slots[command.slot].state.store(SlotState::active,std::memory_order_release);}
            head=(head+1)%queue_count;
        }
        read.store(head,std::memory_order_release);
    }
};
GameAudioMixer::GameAudioMixer(const Json& configuration):impl_(std::make_unique<Impl>(configuration)){}
GameAudioMixer::~GameAudioMixer()=default;
void GameAudioMixer::play(float frequency,float seconds) {
    if(!std::isfinite(frequency)||frequency<=0||frequency>20000||!std::isfinite(seconds)||seconds<=0||seconds>10)throw std::runtime_error("Tone frequency/duration outside supported range");
    impl_->reclaim();Impl::Command command;command.kind=Impl::Kind::tone;command.frequency=frequency;command.seconds=seconds;impl_->push(command);
}
void GameAudioMixer::set_music(std::shared_ptr<const MusicScore> score,bool loop,const std::string& bus){if(!score)stop_music();else impl_->submit(std::move(score),loop,Impl::Kind::music,bus);}
void GameAudioMixer::play_sfx(std::shared_ptr<const MusicScore> score,const std::string& bus){impl_->submit(std::move(score),false,Impl::Kind::sfx,bus);}
void GameAudioMixer::stop_music(){impl_->reclaim();impl_->push({Impl::Kind::stop});}
void GameAudioMixer::stop_all(bool clear_effects){impl_->reclaim();impl_->push({clear_effects?Impl::Kind::stop_all:Impl::Kind::stop_sources});}
void GameAudioMixer::set_bus(const std::string& bus,float gain,float pan,bool mute) {
    if(!std::isfinite(gain)||gain<0||gain>2||!std::isfinite(pan)||pan< -1||pan>1)throw std::runtime_error("Audio bus gain must be 0..2 and pan -1..1");
    Impl::Command command;command.kind=Impl::Kind::bus;command.bus=impl_->bus_index(bus);command.gain=gain;command.pan=pan;command.mute=mute;impl_->reclaim();impl_->push(command);
}
void GameAudioMixer::set_paused(bool paused){impl_->paused.store(paused,std::memory_order_release);}
std::uint64_t GameAudioMixer::dropped_commands()const{return impl_->dropped.load(std::memory_order_relaxed);}
Json GameAudioMixer::measurements()const{return {{"sample_frames",impl_->published_frames.load()},{"peak",impl_->published_peak.load()},{"rms",impl_->published_rms.load()},{"clipped_frames",impl_->published_clipped.load()},{"dropped_commands",dropped_commands()}};}
void GameAudioMixer::render(float* stereo,std::size_t frames) {
    if(!stereo && frames)throw std::runtime_error("Audio render requires a buffer");auto& self=*impl_;self.consume();
    if(self.paused.load(std::memory_order_acquire)){if(frames)std::fill(stereo,stereo+frames*2,0.f);return;}
    std::array<float,512> scratch{};std::array<std::array<float,512>,8> blocks{};
    for(std::size_t position=0;position<frames;) {
        const auto count=std::min<std::size_t>(256,frames-position);
        for(auto& bus:blocks)std::fill(bus.begin(),bus.begin()+count*2,0.f);
        auto mix_voice=[&](int& index){if(index<0)return;auto& slot=self.slots[index];slot.voice->render(scratch.data(),count);for(std::size_t i=0;i<count*2;++i)blocks[slot.bus][i]+=scratch[i];if(slot.voice->finished()){self.retire(index);index=-1;}};
        mix_voice(self.music);for(auto& effect:self.sfx)mix_voice(effect);
        const int tone_bus=self.bus_index("sfx");const double advance=tau*self.frequency/48000;
        for(std::size_t i=0;i<count;++i) {
            if(self.remaining>0){const float tone=float(std::sin(self.phase))*.09f*float(self.remaining)/self.duration;self.phase=std::fmod(self.phase+advance,tau);--self.remaining;blocks[tone_bus][2*i]+=tone;blocks[tone_bus][2*i+1]+=tone;}
            float left=blocks[self.master][2*i],right=blocks[self.master][2*i+1];
            for(std::size_t b=0;b<self.buses.size();++b)if(int(b)!=self.master){auto l=blocks[b][2*i],r=blocks[b][2*i+1];self.buses[b].process(l,r);left+=l;right+=r;}
            self.buses[self.master].process(left,right);
            self.peak=std::max(self.peak,double(std::max(std::abs(left),std::abs(right))));self.energy+=double(left)*left+double(right)*right;
            if(std::abs(left)>1 || std::abs(right)>1)++self.clipped;
            stereo[(position+i)*2]=std::clamp(left,-1.f,1.f);stereo[(position+i)*2+1]=std::clamp(right,-1.f,1.f);
        }
        position+=count;
    }
    self.frames+=frames;self.published_frames.store(self.frames);self.published_peak.store(self.peak);self.published_rms.store(self.frames?std::sqrt(self.energy/(self.frames*2)):0);self.published_clipped.store(self.clipped);
}

Json render_game_audio_wav(const std::map<std::string,std::shared_ptr<const MusicScore>>& scores,const Json& events,
    std::uint64_t ticks,const std::filesystem::path& output,const Json& configuration,const std::string& initial_music) {
    if(ticks>36000 || !events.is_array() || events.size()>1152000)throw std::runtime_error("Audio scenario exceeds 36000 ticks or bounded event count");
    const auto config=validate_audio_mixer(configuration);auto active_config=config;
    auto mixer=std::make_unique<GameAudioMixer>(config);
    auto score=[&](const std::string& id){auto found=scores.find(id);if(found==scores.end())throw std::runtime_error("Unknown music asset in audio event: "+id);return found->second;};
    if(!initial_music.empty())mixer->set_music(score(initial_music));
    std::uint64_t previous=0; std::size_t events_at_tick=0;
    for(const auto& event:events) {
        if(!event.is_object() || !event.contains("tick") || !event["tick"].is_number_integer() || event["tick"]<0 || event["tick"]>ticks)throw std::runtime_error("Audio event tick is outside scenario");
        const auto tick=event["tick"].get<std::uint64_t>();if(tick<previous)throw std::runtime_error("Audio events must be ordered by tick");
        events_at_tick=tick==previous?events_at_tick+1:1;previous=tick;
        // A transition may contain 32 outgoing-hook commands, 32 incoming-hook
        // commands, synthetic music and eight restored bus controls.
        if(events_at_tick>96)throw std::runtime_error("Audio scenario allows at most 96 commands per tick");
        if(!event.contains("action") || !event["action"].is_string())throw std::runtime_error("Audio event requires action");
        const auto action=event["action"].get<std::string>();
        if(action=="music" || action=="sound") {
            if(!event.contains("asset") || !event["asset"].is_string())throw std::runtime_error("Audio event requires asset ID");
            score(event["asset"].get<std::string>());boolean(event,"loop",true);
            const auto bus=event.value("bus",action=="music"?std::string("music"):std::string("sfx"));
            if(!active_config["buses"].contains(bus))throw std::runtime_error("Unknown audio bus: "+bus);
        } else if(action=="bus") {
            if(!event.contains("bus") || !event["bus"].is_string() || !event.contains("gain"))throw std::runtime_error("Audio bus event requires bus and gain");
            if(!active_config["buses"].contains(event["bus"].get<std::string>()))throw std::runtime_error("Unknown audio bus in event");
            number(event,"gain",1,0,2);number(event,"pan",0,-1,1);boolean(event,"mute");
        } else if(action=="configure") {
            active_config=validate_audio_mixer(event.at("configuration"));
            if(event.contains("music")&&!event.at("music").get<std::string>().empty())score(event.at("music").get<std::string>());
            boolean(event,"music_loop",true);
            if(!active_config["buses"].contains(event.value("music_bus",std::string("music"))))throw std::runtime_error("Unknown configured music bus");
        } else if(action!="stop_music" && action!="stop_all")throw std::runtime_error("Unknown audio event action: "+action);
    }
    const std::uint64_t frames=ticks*800+std::uint64_t(std::ceil(active_config["tail_seconds"].get<double>()*48000));
    if(!output.parent_path().empty())std::filesystem::create_directories(output.parent_path());
    std::ofstream out(output,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot write audio scenario WAV");
    auto u16=[&](uint16_t v){out.put(char(v&255));out.put(char(v>>8));};auto u32=[&](uint32_t v){u16(uint16_t(v));u16(uint16_t(v>>16));};
    out.write("RIFF",4);u32(uint32_t(36+frames*4));out.write("WAVEfmt ",8);u32(16);u16(1);u16(2);u32(48000);u32(192000);u16(4);u16(16);out.write("data",4);u32(uint32_t(frames*4));
    std::array<float,1600> buffer{};std::size_t next=0;
    std::uint64_t measured_frames=0,measured_clipped=0,measured_dropped=0;double measured_peak=0,measured_energy=0;
    auto accumulate=[&]{const auto metrics=mixer->measurements();const auto count=metrics["sample_frames"].get<std::uint64_t>();measured_frames+=count;measured_clipped+=metrics["clipped_frames"].get<std::uint64_t>();measured_dropped+=metrics["dropped_commands"].get<std::uint64_t>();measured_peak=std::max(measured_peak,metrics["peak"].get<double>());const auto rms=metrics["rms"].get<double>();measured_energy+=rms*rms*count*2;};
    bool sources_stopped=false;
    for(std::uint64_t position=0;position<frames;) {
        while(next<events.size() && events[next]["tick"].get<std::uint64_t>()*800==position) {
            const auto& event=events[next++];const auto action=event["action"].get<std::string>();
            if(action=="music")mixer->set_music(score(event.at("asset").get<std::string>()),event.value("loop",true),event.value("bus",std::string("music")));
            else if(action=="sound")mixer->play_sfx(score(event.at("asset").get<std::string>()),event.value("bus",std::string("sfx")));
            else if(action=="stop_music")mixer->stop_music();
            else if(action=="stop_all")mixer->stop_all();
            else if(action=="bus")mixer->set_bus(event.at("bus").get<std::string>(),event.at("gain").get<float>(),event.value("pan",0.f),event.value("mute",false));
            else if(action=="configure") {accumulate();mixer=std::make_unique<GameAudioMixer>(event.at("configuration"));const auto music=event.value("music",std::string());if(!music.empty())mixer->set_music(score(music),event.value("music_loop",true),event.value("music_bus",std::string("music")));}
            else throw std::runtime_error("Unknown audio event action: "+action);
        }
        if(position>=ticks*800 && !sources_stopped){mixer->stop_all(false);sources_stopped=true;}
        auto count=std::min<std::uint64_t>(800,frames-position);
        if(next<events.size())count=std::min(count,events[next]["tick"].get<std::uint64_t>()*800-position);
        mixer->render(buffer.data(),std::size_t(count));
        for(std::size_t i=0;i<count*2;++i)u16(uint16_t(int16_t(std::lrint(buffer[i]*32767))));
        position+=count;
    }
    out.close();if(!out)throw std::runtime_error("Audio scenario WAV write incomplete");
    accumulate();Json report{{"sample_frames",measured_frames},{"clipped_frames",measured_clipped},{"dropped_commands",measured_dropped},{"peak",measured_peak},{"rms",measured_frames?std::sqrt(measured_energy/(measured_frames*2)):0}};report["ok"]=true;report["output"]=std::filesystem::absolute(output).string();report["sample_rate"]=48000;report["channels"]=2;report["bits_per_sample"]=16;report["ticks"]=ticks;report["duration_seconds"]=double(frames)/48000;report["tail_seconds"]=active_config["tail_seconds"];return report;
}
}
