#include "langmake/android_input.hpp"
#include "langmake/ui.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>

namespace lm {
namespace {
struct Rect {float x=0,y=0,w=0,h=0;bool contains(float px,float py)const{return px>=x&&py>=y&&px<x+w&&py<y+h;}};
std::string escaped(std::string text){std::string out;for(char c:text){if(c=='&')out+="&amp;";else if(c=='<')out+="&lt;";else if(c=='>')out+="&gt;";else if(c=='\"')out+="&quot;";else out+=c;}return out;}
SDL_Event mouse_motion(float x,float y){SDL_Event event{};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.x=x;event.motion.y=y;return event;}
SDL_Event mouse_button(bool down){SDL_Event event{};event.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;event.button.button=SDL_BUTTON_LEFT;return event;}
}
struct MobileControls::Impl {
    enum class Kind {stick,action,ui,ui_action,ignored};
    struct Finger {Kind kind=Kind::ignored;std::string action;bool held=false;std::int64_t pointer=0;};
    using Key=std::pair<SDL_TouchID,SDL_FingerID>;
    std::map<Key,Finger> fingers;
    std::int64_t next_pointer=0;
    std::set<std::string> pulses;
    std::vector<std::string> actions;
    std::map<std::string,std::string> labels;
    Json configuration;
    bool enabled=true,stick_enabled=true,pointer_pending=false;
    std::string x_action="move_x",y_action="move_z";
    std::vector<std::pair<std::string,Rect>> buttons;
    std::filesystem::path runtime;
    bool headless=false;
    int width=0,height=0,page=0;
    float density=1,stick_x=0,stick_y=0,radius=1,move_x=0,move_z=0;
    SDL_Rect safe{};
    Rect page_button;
    std::array<float,2> pointer{};
    std::unique_ptr<UiRenderer> overlay;
    bool has(Kind kind)const{for(const auto&[key,finger]:fingers)if(finger.kind==kind)return true;return false;}
    bool held(const std::string& action)const{for(const auto&[key,finger]:fingers)if(finger.kind==Kind::action&&finger.action==action&&finger.held)return true;return false;}
    void rebuild() {
        if(width<1||height<1)return;
        buttons.clear();overlay.reset();if(!enabled)return;
        radius=std::max(24.f,std::min({safe.w*.16f,safe.h*.16f,88*density}));
        stick_x=safe.x+radius+16*density;stick_y=safe.y+safe.h-radius-16*density;
        const float gap=std::max(4.f,8*density),left=safe.x+safe.w*.48f;
        const float bw=std::max(20.f,(safe.x+safe.w-left-3*gap)/3),bh=std::max(24.f,std::min(52*density,safe.h*.12f));
        const float top=safe.y+safe.h-2*bh-2*gap;
        for(int i=0;i<6&&page*6+i<int(actions.size());++i)buttons.push_back({actions[page*6+i],{left+(i%3)*(bw+gap),top+(i/3)*(bh+gap),bw,bh}});
        page_button={left,top-bh-gap,3*bw+2*gap,bh};
        Project project;project.ui={{"documents",{{"controls","__mobile__/controls.rml"}}},{"styles",{{"controls","__mobile__/controls.rcss"}}}};
        std::ostringstream css,document;
        css<<"body {width:100%;height:100%;pointer-events:none;color:#ffffff;font-size:"<<std::max(12.f,std::min(16*density,bw/5))<<"px;} .action {position:absolute;border:1px #a1bdd8;background-color:#172942c0;text-align:center;border-radius:8px;} .held {background-color:#356c9cdd;} #stick {position:absolute;left:"<<stick_x-radius<<"px;top:"<<stick_y-radius<<"px;width:"<<2*radius<<"px;height:"<<2*radius<<"px;border:2px #7894ad;background-color:#15273ba0;border-radius:"<<radius<<"px;} #knob {position:absolute;width:30%;height:30%;left:35%;top:35%;border-radius:"<<radius<<"px;background-color:#a7cfe9;} #knob.n {top:8%;} #knob.s {top:62%;} #knob.w {left:8%;} #knob.e {left:62%;}";
        document<<"<rml><head><link type='text/rcss' href='controls.rcss'/></head><body>";
        if(stick_enabled)document<<"<div id='stick'><div id='knob'/></div>";
        auto add=[&](const std::string& id,const std::string& label,const Rect&r){css<<"#"<<id<<" {left:"<<r.x<<"px;top:"<<r.y<<"px;width:"<<r.w<<"px;height:"<<r.h<<"px;line-height:"<<r.h<<"px;}";document<<"<div id='"<<id<<"' class='action'>"<<escaped(label)<<"</div>";};
        for(std::size_t i=0;i<buttons.size();++i)add("action_"+std::to_string(i),labels.at(buttons[i].first),buttons[i].second);
        if(actions.size()>6)add("page","Actions "+std::to_string(page+1)+" / "+std::to_string((actions.size()+5)/6),page_button);
        document<<"</body></rml>";project.sources["__mobile__/controls.rml"]=document.str();project.sources["__mobile__/controls.rcss"]=css.str();
        overlay=std::make_unique<UiRenderer>(project,runtime,headless);
    }
    void stick(float x,float y){move_x=(x-stick_x)/radius;move_z=(y-stick_y)/radius;const float length=std::hypot(move_x,move_z);if(length<.15f)move_x=move_z=0;else if(length>1){move_x/=length;move_z/=length;}}
};
MobileControls::MobileControls(const Json& input,const std::filesystem::path& runtime,bool headless):impl_(std::make_unique<Impl>()){impl_->runtime=runtime;impl_->headless=headless;configure(input);}
MobileControls::~MobileControls()=default;
void MobileControls::configure(const Json& input){
    auto& s=*impl_;if(s.configuration==input)return;
    const auto touch=input.value("touch",Json::object());const auto stick=touch.value("stick",Json::object());
    std::vector<std::string> actions;std::map<std::string,std::string> labels;
    auto registered=[&](const std::string& name){return name=="jump"||name=="fire"||(input.contains("actions")&&input["actions"].contains(name));};
    auto add=[&](const std::string& name,const std::string& label){if(!registered(name)||label.empty()||label.size()>96||labels.contains(name))throw std::runtime_error("Invalid mobile action or label");actions.push_back(name);labels[name]=label;};
    if(touch.contains("buttons")){if(!touch["buttons"].is_array()||touch["buttons"].size()>64)throw std::runtime_error("Mobile buttons must contain at most 64 actions");for(const auto& button:touch["buttons"])add(button.at("action").get<std::string>(),button.value("label",button.at("action").get<std::string>()));}
    else {add("jump","Jump");add("fire","Fire");if(input.contains("actions"))for(const auto&[name,binding]:input["actions"].items())if(!labels.contains(name))add(name,name);}
    if(actions.size()>130)throw std::runtime_error("Mobile action budget exceeded");
    const auto x=stick.value("x_action",std::string("move_x")),y=stick.value("y_action",std::string("move_z"));
    auto axis_valid=[&](const std::string& name){return name=="move_x"||name=="move_z"||(input.contains("actions")&&input["actions"].contains(name));};
    if(!axis_valid(x)||!axis_valid(y)||x==y)throw std::runtime_error("Mobile stick axes require distinct registered actions");
    reset();s.configuration=input;s.actions=std::move(actions);s.labels=std::move(labels);s.enabled=touch.value("enabled",true);s.stick_enabled=stick.value("enabled",true);s.x_action=x;s.y_action=y;s.page=0;s.rebuild();
}
void MobileControls::resize(int width,int height,float density,SDL_Rect safe,UiRenderer* game_ui){auto&s=*impl_;if(width<1||height<1||!std::isfinite(density)||density<=0||density>16)throw std::runtime_error("Invalid mobile viewport");safe.x=std::clamp(safe.x,0,width-1);safe.y=std::clamp(safe.y,0,height-1);safe.w=std::clamp(safe.w,1,width-safe.x);safe.h=std::clamp(safe.h,1,height-safe.y);if(s.width==width&&s.height==height&&s.density==density&&s.safe.x==safe.x&&s.safe.y==safe.y&&s.safe.w==safe.w&&s.safe.h==safe.h)return;reset(game_ui);s.width=width;s.height=height;s.density=density;s.safe=safe;s.rebuild();}
bool MobileControls::process_event(const SDL_Event& event,UiRenderer& game_ui){
    if(event.type!=SDL_EVENT_FINGER_DOWN&&event.type!=SDL_EVENT_FINGER_MOTION&&event.type!=SDL_EVENT_FINGER_UP&&event.type!=SDL_EVENT_FINGER_CANCELED)return false;
    auto&s=*impl_;const Impl::Key key{event.tfinger.touchID,event.tfinger.fingerID};const float x=std::clamp(event.tfinger.x,0.f,1.f)*s.width,y=std::clamp(event.tfinger.y,0.f,1.f)*s.height;s.pointer={std::clamp(event.tfinger.x,0.f,1.f),std::clamp(event.tfinger.y,0.f,1.f)};
    if(event.type==SDL_EVENT_FINGER_DOWN){
        if(s.fingers.contains(key)||s.fingers.size()>=16)return true;
        s.pointer_pending=true;auto& finger=s.fingers[key];finger.pointer=++s.next_pointer;
        if(game_ui.process_pointer(finger.pointer,UiRenderer::PointerPhase::down,x,y)){finger.kind=Impl::Kind::ui_action;return true;}
        if(!s.has(Impl::Kind::ui)){game_ui.process_event(mouse_motion(x,y));if(game_ui.process_event(mouse_button(true))){finger.kind=Impl::Kind::ui;return true;}game_ui.process_event(mouse_button(false));}
        if(!s.enabled)return true;
        if(s.actions.size()>6&&s.page_button.contains(x,y)){s.page=(s.page+1)%int((s.actions.size()+5)/6);s.pulses.clear();for(auto&[id,f]:s.fingers)if(f.kind==Impl::Kind::action)f.kind=Impl::Kind::ignored;s.rebuild();return true;}
        for(const auto&[action,rect]:s.buttons)if(rect.contains(x,y)){finger.kind=Impl::Kind::action;finger.action=action;finger.held=true;s.pulses.insert(action);return true;}
        if(s.stick_enabled&&!s.has(Impl::Kind::stick)&&std::hypot(x-s.stick_x,y-s.stick_y)<s.radius*1.4f){finger.kind=Impl::Kind::stick;s.stick(x,y);}
        return true;
    }
    auto found=s.fingers.find(key);if(found==s.fingers.end())return true;auto& finger=found->second;
    const bool released=event.type==SDL_EVENT_FINGER_UP||event.type==SDL_EVENT_FINGER_CANCELED;
    s.pointer_pending=event.type!=SDL_EVENT_FINGER_CANCELED;
    if(finger.kind==Impl::Kind::ui_action)game_ui.process_pointer(finger.pointer,event.type==SDL_EVENT_FINGER_CANCELED?UiRenderer::PointerPhase::cancel:released?UiRenderer::PointerPhase::up:UiRenderer::PointerPhase::move,x,y);
    else if(finger.kind==Impl::Kind::ui){game_ui.process_event(mouse_motion(event.type==SDL_EVENT_FINGER_CANCELED?-100:x,event.type==SDL_EVENT_FINGER_CANCELED?-100:y));if(released)game_ui.process_event(mouse_button(false));}
    else if(finger.kind==Impl::Kind::stick){if(released)s.move_x=s.move_z=0;else s.stick(x,y);}
    else if(finger.kind==Impl::Kind::action){finger.held=false;if(!released)for(const auto&[action,rect]:s.buttons)if(action==finger.action)finger.held=rect.contains(x,y);if(event.type==SDL_EVENT_FINGER_CANCELED)s.pulses.erase(finger.action);}
    if(released)s.fingers.erase(found);return true;
}
void MobileControls::apply(Input& input)const{const auto&s=*impl_;if(s.has(Impl::Kind::stick)){auto axis=[&](const std::string& name,float value){if(name=="move_x")input.move_x=value;else if(name=="move_z")input.move_z=value;else input.actions[name]=value;};axis(s.x_action,s.move_x);axis(s.y_action,s.move_z);}for(const auto& action:s.actions){const bool active=s.held(action)||s.pulses.contains(action);if(action=="jump")input.jump=input.jump||s.pulses.contains(action);else if(action=="fire")input.fire=input.fire||active;else if(active)input.actions[action]=1;}if(!s.fingers.empty()||s.pointer_pending)input.pointer=s.pointer;}
void MobileControls::consume_tick(){impl_->pulses.clear();impl_->pointer_pending=false;}
void MobileControls::reset(UiRenderer* game_ui){auto&s=*impl_;if(game_ui)game_ui->reset_input();if(game_ui&&s.has(Impl::Kind::ui)){game_ui->process_event(mouse_motion(-100,-100));game_ui->process_event(mouse_button(false));}s.fingers.clear();s.pulses.clear();s.pointer_pending=false;s.move_x=s.move_z=0;}
void MobileControls::draw(double time){auto&s=*impl_;if(!s.overlay)return;Json state;state["knob"]={{"class",std::string(s.move_z<-.2?"n ":s.move_z>.2?"s ":"")+(s.move_x<-.2?"w":s.move_x>.2?"e":"")}};for(std::size_t i=0;i<s.buttons.size();++i)state["action_"+std::to_string(i)]={{"class",s.held(s.buttons[i].first)?"action held":"action"}};s.overlay->update({{"ui",state}},time,s.width,s.height,s.density);s.overlay->draw();}
Json MobileControls::inspect()const{const auto&s=*impl_;Json buttons=Json::array();for(const auto&[name,r]:s.buttons)buttons.push_back({{"action",name},{"label",s.labels.at(name)},{"rect",{r.x,r.y,r.w,r.h}},{"held",s.held(name)}});return {{"enabled",s.enabled},{"stick_enabled",s.stick_enabled},{"x_action",s.x_action},{"y_action",s.y_action},{"page",s.page},{"pages",(s.actions.size()+5)/6},{"fingers",s.fingers.size()},{"move",{s.move_x,s.move_z}},{"stick",{s.stick_x,s.stick_y,s.radius}},{"buttons",buttons},{"page_rect",{s.page_button.x,s.page_button.y,s.page_button.w,s.page_button.h}},{"safe_area",{s.safe.x,s.safe.y,s.safe.w,s.safe.h}}};}
}
