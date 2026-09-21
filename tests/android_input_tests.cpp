#include "langmake/android_input.hpp"
#include "langmake/android_recovery.hpp"
#include "langmake/ui.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using lm::Json;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action){try{action();}catch(const std::exception&){return;}throw std::runtime_error("Invalid touch input accepted");}
SDL_Event finger(Uint32 type,SDL_FingerID id,float x,float y,int width=1000,int height=600){SDL_Event e{};e.type=type;e.tfinger.fingerID=id;e.tfinger.touchID=1;e.tfinger.x=x/width;e.tfinger.y=y/height;return e;}
std::array<float,2> center(const Json& rectangle){return {rectangle[0].get<float>()+rectangle[2].get<float>()*.5f,rectangle[1].get<float>()+rectangle[3].get<float>()*.5f};}
lm::Project authored_ui(){lm::Project p;p.ui={{"documents",{{"hud","ui/hud.rml"}}},{"styles",{{"main","ui/main.rcss"}}}};p.sources["ui/hud.rml"]="<rml><head><link type='text/rcss' href='main.rcss'/></head><body><button id='dialogue'>Continue</button></body></rml>";p.sources["ui/main.rcss"]="body {width:100%;height:100%;} #dialogue {position:absolute;left:400px;top:10px;width:200px;height:60px;}";return p;}
void touch_routing(const std::filesystem::path& runtime){
    auto p=authored_ui();lm::UiRenderer ui(p,runtime,true);ui.update(Json::object(),0,1000,600);ui.draw();
    lm::MobileControls controls(Json::object(),runtime,true);controls.resize(1000,600,1,{20,10,960,570},&ui);controls.draw(0);
    const auto layout=controls.inspect();const auto stick=layout["stick"];const float sx=stick[0],sy=stick[1],radius=stick[2];
    const auto fire=center(layout["buttons"][1]["rect"]),jump=center(layout["buttons"][0]["rect"]);
    controls.process_event(finger(SDL_EVENT_FINGER_DOWN,1,sx+radius*.8f,sy),ui);
    controls.process_event(finger(SDL_EVENT_FINGER_DOWN,2,fire[0],fire[1]),ui);
    lm::Input input;controls.apply(input);check(input.move_x>.7f&&input.fire,"Independent stick and action fingers must combine");
    controls.consume_tick();controls.process_event(finger(SDL_EVENT_FINGER_UP,2,fire[0],fire[1]),ui);input={};controls.apply(input);check(input.move_x>.7f&&!input.fire,"Action release must not release the stick");
    controls.process_event(finger(SDL_EVENT_FINGER_DOWN,3,jump[0],jump[1]),ui);controls.process_event(finger(SDL_EVENT_FINGER_UP,3,jump[0],jump[1]),ui);
    input={};controls.apply(input);check(input.jump,"A tap between simulation ticks must survive until consumption");controls.consume_tick();input={};controls.apply(input);check(!input.jump,"Consumed jump tap must not repeat");
    controls.process_event(finger(SDL_EVENT_FINGER_DOWN,4,500,35),ui);controls.process_event(finger(SDL_EVENT_FINGER_UP,4,500,35),ui);
    auto events=ui.drain_events();check(events.size()==1&&events[0]["id"]=="dialogue"&&events[0]["event"]=="click","Authored UI must emit semantic touch clicks");
    controls.process_event(finger(SDL_EVENT_FINGER_DOWN,5,500,35),ui);controls.reset(&ui);events=ui.drain_events();check(events.empty(),"Cancellation must not activate a pressed UI button");input={};controls.apply(input);check(input.move_x==0&&!input.fire&&!input.jump&&controls.inspect()["fingers"]==0,"Lifecycle reset must release every finger and pulse");
    // Resize/density changes cancel the authored pointer too, avoiding stuck UI.
    controls.process_event(finger(SDL_EVENT_FINGER_DOWN,6,500,35),ui);controls.resize(1000,600,2,{20,10,960,570},&ui);controls.process_event(finger(SDL_EVENT_FINGER_UP,6,500,35),ui);check(ui.drain_events().empty(),"Viewport change must cancel an owned UI pointer");
    for(const auto& button:controls.inspect()["buttons"]){auto rect=button["rect"];check(rect[0].get<float>()>=20&&rect[1].get<float>()>=10&&rect[0].get<float>()+rect[2].get<float>()<=980.1f&&rect[1].get<float>()+rect[3].get<float>()<=580.1f,"Touch actions must stay inside the safe area");}
}
void authoring_and_pages(const std::filesystem::path& runtime){
    lm::Project empty;lm::UiRenderer ui(empty,runtime,true);
    Json config;config["actions"]={{"aim_x",Json::object()},{"aim_y",Json::object()},{"launch",Json::object()}};
    config["touch"]={{"stick",{{"x_action","aim_x"},{"y_action","aim_y"}}},{"buttons",Json::array({{{"action","launch"},{"label","Launch!"}}})}};
    lm::MobileControls controls(config,runtime,true);controls.resize(1000,600,1,{0,0,1000,600});auto layout=controls.inspect();check(layout["buttons"].size()==1&&layout["buttons"][0]["label"]=="Launch!","Explicit ordered action subset and labels must apply");
    const auto stick=layout["stick"];controls.process_event(finger(SDL_EVENT_FINGER_DOWN,1,stick[0].get<float>()+stick[2].get<float>(),stick[1].get<float>()),ui);lm::Input input;controls.apply(input);check(input.move_x==0&&input.actions["aim_x"]>.99f,"Virtual stick must route to authored named axes");
    config["touch"]["enabled"]=false;controls.configure(config);layout=controls.inspect();check(!layout["enabled"].get<bool>()&&layout["buttons"].empty(),"Games can disable generated controls");
    Json many={{"actions",Json::object()}};for(int i=0;i<20;++i)many["actions"]["action"+std::to_string(i)]=Json::object();controls.configure(many);layout=controls.inspect();check(layout["buttons"].size()==6&&layout["pages"]==4,"Many named actions must paginate without omitting them");
    const auto next=center(layout["page_rect"]);controls.process_event(finger(SDL_EVENT_FINGER_DOWN,2,next[0],next[1]),ui);check(controls.inspect()["page"]==1,"Page button must advance to remaining actions");controls.reset(&ui);
    auto broken=config;broken["touch"]["stick"]["x_action"]="missing";rejects([&]{controls.configure(broken);});
    rejects([&]{controls.resize(0,600,1,{0,0,1,1});});
}
void overlapping_ui(const std::filesystem::path& runtime){
    lm::MobileControls controls(Json::object(),runtime,true);controls.resize(1000,600,1,{0,0,1000,600});
    auto rectangle=controls.inspect()["buttons"][1]["rect"];auto point=center(rectangle);auto project=authored_ui();
    project.sources["ui/main.rcss"]="body {width:100%;height:100%;} #dialogue {position:absolute;left:"+std::to_string(rectangle[0].get<float>())+"px;top:"+std::to_string(rectangle[1].get<float>())+"px;width:"+std::to_string(rectangle[2].get<float>())+"px;height:"+std::to_string(rectangle[3].get<float>())+"px;}";
    lm::UiRenderer ui(project,runtime,true);ui.update(Json::object(),0,1000,600);ui.draw();
    controls.process_event(finger(SDL_EVENT_FINGER_DOWN,1,point[0],point[1]),ui);lm::Input input;controls.apply(input);check(!input.fire,"Authored UI overlapping a generated control must own the touch");
    controls.process_event(finger(SDL_EVENT_FINGER_UP,1,point[0],point[1]),ui);check(ui.drain_events().size()==1,"Overlapping UI must receive its semantic click");
    controls.configure({{"touch",{{"enabled",false}}}});
    controls.process_event(finger(SDL_EVENT_FINGER_DOWN,2,point[0],point[1]),ui);controls.process_event(finger(SDL_EVENT_FINGER_UP,2,point[0],point[1]),ui);check(ui.drain_events().size()==1,"Disabling generated controls must preserve authored UI touch");
}
void custom_multitouch(const std::filesystem::path& runtime){
    auto p=authored_ui();p.document={{"input",{{"actions",{{"dash",Json::object()}}}}}};
    p.sources["ui/hud.rml"]="<rml><head><link type='text/rcss' href='main.rcss'/></head><body><button id='fire' data-action='fire' data-hit-padding='10'>Fire</button><button id='dash' data-action='dash'>Dash</button></body></rml>";
    p.sources["ui/main.rcss"]="body {width:100%;height:100%;} button {position:absolute;top:20px;width:100px;height:60px;} #fire {left:400px;} #dash {left:600px;}";
    lm::UiRenderer ui(p,runtime,true);ui.update(Json::object(),0,1000,600);
    lm::MobileControls controls(Json::object(),runtime,true);controls.resize(1000,600,1,{0,0,1000,600},&ui);const auto stick=controls.inspect()["stick"];
    auto input=[&]{lm::Input value;controls.apply(value);ui.apply(value);return value;};
    controls.process_event(finger(SDL_EVENT_FINGER_DOWN,1,stick[0].get<float>()+stick[2].get<float>(),stick[1].get<float>()),ui);
    controls.process_event(finger(SDL_EVENT_FINGER_DOWN,2,395,40),ui);controls.process_event(finger(SDL_EVENT_FINGER_DOWN,3,650,40),ui);
    check(input().move_x>.9f&&input().fire&&input().actions["dash"]==1,"Native stick and two custom held buttons must use three independent fingers");
    controls.consume_tick();ui.consume_tick();controls.process_event(finger(SDL_EVENT_FINGER_MOTION,2,800,400),ui);check(!input().fire&&input().actions["dash"]==1&&input().move_x>.9f,"Leaving custom hit region must preserve other fingers");
    controls.process_event(finger(SDL_EVENT_FINGER_MOTION,2,450,40),ui);check(input().fire,"Reentering owned custom control must restore held action");
    controls.process_event(finger(SDL_EVENT_FINGER_CANCELED,2,450,40),ui);check(!input().fire&&input().actions["dash"]==1,"Canceled finger must never keep or transfer firing");
    controls.reset(&ui);check(input().move_x==0&&input().actions["dash"]==0&&ui.inspect()["owned_pointers"]==0,"Lifecycle reset must clear native and custom held inputs together");
    controls.process_event(finger(SDL_EVENT_FINGER_DOWN,4,450,40),ui);controls.process_event(finger(SDL_EVENT_FINGER_UP,4,450,40),ui);check(input().fire,"Custom touch tap must survive one fixed tick");ui.consume_tick();check(!input().fire,"Custom touch tap must not repeat");
    controls.process_event(finger(SDL_EVENT_FINGER_DOWN,5,450,40),ui);controls.resize(1000,600,2,{20,10,960,580},&ui);check(!input().fire,"Rotation/density changes must cancel custom fingers");
}
void released_pointer_position(const std::filesystem::path& runtime){
    auto p=authored_ui();p.document={{"input",{{"actions",{{"drop",Json::object()}}}}}};
    p.sources["ui/hud.rml"]="<rml><head><link type='text/rcss' href='main.rcss'/></head><body><button id='dialogue' data-action='drop' data-event='click'>Drop</button></body></rml>";
    lm::UiRenderer ui(p,runtime,true);ui.update(Json::object(),0,1000,600);
    lm::MobileControls controls({{"touch",{{"enabled",false}}}},runtime,true);controls.resize(1000,600,1,{0,0,1000,600},&ui);
    controls.process_event(finger(SDL_EVENT_FINGER_DOWN,1,450,35),ui);
    controls.process_event(finger(SDL_EVENT_FINGER_UP,1,550,45),ui);
    lm::Input input;input.pointer={.1f,.2f};controls.apply(input);ui.apply(input);
    check(input.actions["drop"]==1&&std::abs(input.pointer[0]-.55f)<.0001f&&std::abs(input.pointer[1]-.075f)<.0001f,"Quick semantic drop must retain release coordinates for its consuming tick");
    check(ui.drain_events().size()==3,"Custom tap must emit press, release and committed click");
    controls.consume_tick();ui.consume_tick();input={};input.pointer={.1f,.2f};controls.apply(input);
    check(input.pointer[0]==.1f&&input.pointer[1]==.2f,"Consumed touch must stop overriding mouse input");
    controls.process_event(finger(SDL_EVENT_FINGER_DOWN,2,450,35),ui);controls.reset(&ui);input={};input.pointer={.1f,.2f};controls.apply(input);
    check(input.pointer[0]==.1f&&input.pointer[1]==.2f,"Lifecycle reset must discard pending touch coordinates");
}
void recovery_quarantine(const std::filesystem::path& runtime){
    const auto directory=runtime/("android-recovery-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(directory);
    const auto active=directory/"_android_resume.json",rejected=directory/"_android_resume.rejected.json";
    auto write=[&](const std::string& bytes){std::ofstream stream(active,std::ios::binary);stream<<bytes;};
    const std::string corrupt="{corrupt save bytes";write(corrupt);auto result=lm::quarantine_android_recovery(active,"Invalid JSON");
    check(result["preserved"]==true&&!std::filesystem::exists(active)&&lm::read_shared_file(rejected)==corrupt,"Rejected corrupt recovery bytes must be preserved before autosaving");
    check(lm::read_json_shared(directory/"_android_resume.rejected.json.reason.json")["diagnostic"]=="Invalid JSON","Rejected recovery must retain its diagnostic");
    write("second rejected save");result=lm::quarantine_android_recovery(active,"Revision mismatch");check(result["preserved"]==true&&lm::read_shared_file(rejected)=="second rejected save","Quarantine must retain exactly the most recent rejected slot");
    std::filesystem::remove(rejected);std::filesystem::create_directory(rejected);write("do not overwrite");result=lm::quarantine_android_recovery(active,"Mismatch");
    check(result["preserved"]==false&&lm::read_shared_file(active)=="do not overwrite","Failed preservation must keep original recovery and block autosaves");
}
}
int main(int argc,char** argv){try{auto runtime=argc>1?std::filesystem::path(argv[1]):std::filesystem::path("build");touch_routing(runtime);authoring_and_pages(runtime);overlapping_ui(runtime);custom_multitouch(runtime);released_pointer_position(runtime);recovery_quarantine(runtime);std::cout<<"PASS mobile custom-action multitouch with native stick, ownership/cancel/overlap, quick taps, safe area/density, named axes, explicit controls, action pages, rejected recovery preservation\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
