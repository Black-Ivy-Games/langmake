#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#include <miniaudio.h>
#include "langmake/audio.hpp"
#include "langmake/audio_mixer.hpp"
#include <atomic>

namespace lm {
struct Audio::Impl {
    ma_device device{};
    std::unique_ptr<GameAudioMixer> mixer;
    bool ready=false;
    bool requested=false;
    bool null_backend=false;
    std::string backend="none",initialization_status="disabled",last_operation="none";
    ma_result last_result=MA_SUCCESS;
    bool paused=false;
    bool suspended=false;
    std::atomic<bool> interrupted{false},notification_pending{false};
    explicit Impl(const Json& configuration):mixer(std::make_unique<GameAudioMixer>(configuration)){}
    ma_result record(const char* operation,ma_result result){last_operation=operation;last_result=result;return result;}
    static void callback(ma_device* device,void* out,const void*,ma_uint32 frames) {
        static_cast<Impl*>(device->pUserData)->mixer->render(static_cast<float*>(out),frames);
    }
    static void notification(const ma_device_notification* event) {
        auto* self=static_cast<Impl*>(event->pDevice->pUserData);
        if(event->type==ma_device_notification_type_interruption_began){self->interrupted=true;self->notification_pending=true;}
        else if(event->type==ma_device_notification_type_interruption_ended){self->interrupted=false;self->notification_pending=true;}
    }
};
Audio::Audio(bool enabled,const Json& configuration):impl_(std::make_unique<Impl>(configuration)) {
    impl_->requested=enabled;
    if(!enabled)return;
    auto config=ma_device_config_init(ma_device_type_playback);
    config.playback.format=ma_format_f32;config.playback.channels=2;config.sampleRate=48000;
    config.dataCallback=Impl::callback;config.notificationCallback=Impl::notification;config.pUserData=impl_.get();
    if(impl_->record("initialize",ma_device_init(nullptr,&config,&impl_->device))!=MA_SUCCESS){impl_->initialization_status="init_failed";return;}
    impl_->backend=ma_get_backend_name(impl_->device.pContext->backend);
    impl_->null_backend=impl_->device.pContext->backend==ma_backend_null;
    if(impl_->record("start",ma_device_start(&impl_->device))==MA_SUCCESS){impl_->ready=true;impl_->initialization_status=impl_->null_backend?"null_backend":"ready";}
    else {impl_->initialization_status="start_failed";ma_device_uninit(&impl_->device);}
}
Audio::~Audio(){if(impl_->ready)ma_device_uninit(&impl_->device);}
void Audio::play(float frequency,float seconds){if(impl_->ready)impl_->mixer->play(frequency,seconds);}
void Audio::set_music(std::shared_ptr<const MusicScore> score,bool loop,const std::string& bus){if(impl_->ready)impl_->mixer->set_music(std::move(score),loop,bus);}
void Audio::stop_music(){if(impl_->ready)impl_->mixer->stop_music();}
void Audio::stop_all(){if(impl_->ready)impl_->mixer->stop_all();}
void Audio::play_sfx(std::shared_ptr<const MusicScore> score,const std::string& bus){if(impl_->ready)impl_->mixer->play_sfx(std::move(score),bus);}
void Audio::set_bus(const std::string& bus,float gain,float pan,bool mute){if(impl_->ready)impl_->mixer->set_bus(bus,gain,pan,mute);}
void Audio::configure(const Json& configuration){
    auto next=std::make_unique<GameAudioMixer>(configuration);next->set_paused(impl_->paused);
    if(impl_->ready&&!impl_->suspended&&impl_->record("configure_stop",ma_device_stop(&impl_->device))!=MA_SUCCESS)throw std::runtime_error("Could not stop audio device for mixer reconfiguration");
    impl_->mixer=std::move(next);
    if(impl_->ready&&!impl_->suspended&&!impl_->interrupted&&impl_->record("configure_start",ma_device_start(&impl_->device))!=MA_SUCCESS){ma_device_uninit(&impl_->device);impl_->ready=false;}
}
void Audio::set_paused(bool paused){impl_->paused=paused;impl_->mixer->set_paused(paused);}
bool Audio::set_suspended(bool suspended){
    if(impl_->suspended==suspended)return true;
    if(!impl_->ready){impl_->suspended=suspended;return true;}
    if(suspended){if(impl_->record("suspend",ma_device_stop(&impl_->device))!=MA_SUCCESS)return false;impl_->suspended=true;}
    else {
        if(!impl_->interrupted&&impl_->record("resume",ma_device_start(&impl_->device))!=MA_SUCCESS){ma_device_uninit(&impl_->device);impl_->ready=false;return false;}
        impl_->suspended=false;
    }
    return true;
}
bool Audio::pump_device(){
    if(impl_->notification_pending.exchange(false)&&impl_->ready&&!impl_->suspended&&!impl_->interrupted){
        if(impl_->record("interruption_resume",ma_device_start(&impl_->device))!=MA_SUCCESS){ma_device_uninit(&impl_->device);impl_->ready=false;return false;}
    }
    return true;
}
bool Audio::available()const{return impl_->ready&&!impl_->null_backend;}
std::uint64_t Audio::dropped_commands()const{return impl_->mixer->dropped_commands();}
Json Audio::measurements()const{
    auto result=impl_->mixer->measurements();result["device_interrupted"]=impl_->interrupted.load();result["device_suspended"]=impl_->suspended;
    result["device_requested"]=impl_->requested;result["device_backend"]=impl_->backend;result["device_is_null"]=impl_->null_backend;
    result["device_initialization_status"]=impl_->initialization_status;result["device_ready"]=impl_->ready;result["output_available"]=available();
    result["device_last_operation"]=impl_->last_operation;result["device_last_result"]=int(impl_->last_result);
    result["device_last_error"]=impl_->last_result==MA_SUCCESS?"":ma_result_description(impl_->last_result);
    return result;
}
}
