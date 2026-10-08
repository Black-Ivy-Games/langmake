#include "langmake/ui.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <set>

namespace {
using lm::Json;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action){try{action();}catch(const std::runtime_error&){return;}throw std::runtime_error("Invalid UI accepted");}
lm::Project project(){
    lm::Project result;result.ui={{"documents",{{"hud","ui/hud.rml"}}},{"styles",{{"main","ui/main.rcss"}}}};
    result.sources["ui/main.rcss"]="body {width:100%;height:100%;color:#ffffff;} #panel {position:absolute;right:20px;top:10px;width:200px;height:160px;background-color:#18354b;} #score {height:30px;} #health {width:180px;height:20px;background-color:#333333;} #health fill {background-color:#66cc55;} button {width:120px;height:30px;display:block;background-color:#224466;tab-index:auto;} button:focus {background-color:#557799;}";
    result.sources["ui/hud.rml"]="<rml><head><title>Test</title><link type='text/rcss' href='main.rcss'/></head><body><div id='panel'><div id='score'>0</div><progress id='health' value='1' max='1'/><button id='next'>Next</button><button id='cancel'>Cancel</button></div></body></rml>";
    return result;
}
void layout_input(const std::filesystem::path& executable){
    auto authored=project();lm::UiRenderer ui(authored,executable,true);
    const Json state={{"ui",{{"score",42},{"health",{{"value",.75},{"max",1}}}}}};
    ui.update(state,1,1280,800);ui.draw();auto first=ui.inspect();
    check(first["elements"]["score"]["value"]==42,"UI scalar binding missing");
    check(first["editor_revision"]==1&&first["elements"]["panel"]["document"]=="ui/hud.rml","Editor must identify native document");
    check(first["elements"]["panel"]["properties"]["position"]=="absolute"&&first["elements"]["panel"]["content_size"][0]==200,"Editor must expose native styles/content box");
    check(std::abs(first["elements"]["panel"]["position"][0].get<float>()-1060)<1,"Right anchor incorrect");
    ui.update(state,1,1600,900);ui.draw();auto resized=ui.inspect();
    check(std::abs(resized["elements"]["panel"]["position"][0].get<float>()-1380)<1,"Resize must preserve screen-space anchoring");
    SDL_Event event{};event.type=SDL_EVENT_KEY_DOWN;event.key.key=SDLK_TAB;check(ui.process_event(event)&&ui.captures_keyboard(),"Tab must focus UI");
    event.key.key=SDLK_RETURN;check(ui.process_event(event),"UI activation must consume key");auto events=ui.drain_events();
    check(events.size()==1&&events[0]["id"]=="next"&&events[0]["event"]=="click"&&events[0]["type"]=="ui","Button must emit stable semantic input event");
    event.type=SDL_EVENT_GAMEPAD_BUTTON_DOWN;event.gbutton.button=SDL_GAMEPAD_BUTTON_DPAD_DOWN;check(ui.process_event(event),"Gamepad must navigate focus");
    event.gbutton.button=SDL_GAMEPAD_BUTTON_SOUTH;check(ui.process_event(event),"Gamepad must activate focused UI");events=ui.drain_events();check(events.size()==1&&events[0]["id"]=="cancel","Gamepad activation targeted wrong button");
    event={};event.type=SDL_EVENT_KEY_DOWN;event.key.key=SDLK_ESCAPE;event.key.scancode=SDL_SCANCODE_ESCAPE;
    check(ui.process_event(event)&&!ui.captures_keyboard(),"First Escape must cancel focused UI");
    events=ui.drain_events();check(events.size()==1&&events[0]["event"]=="cancel","Escape cancellation must be observable");
    check(!ui.process_event(event),"Unfocused Escape must reach the player/preview exit handler");
    event.key.key=SDLK_TAB;ui.process_event(event);
    event.key.key=SDLK_AC_BACK;event.key.scancode=SDL_SCANCODE_AC_BACK;
    check(ui.process_event(event)&&!ui.captures_keyboard(),"Android Back must cancel focused UI before exiting");
    events=ui.drain_events();check(events.size()==1&&events[0]["event"]=="cancel","Android Back cancellation must be observable");
    check(!ui.process_event(event),"Unfocused Android Back must reach the player exit handler");
    ui.update({{"ui",{{"score",{{"text","<button id='injected'>unsafe</button>"},{"visible",false}}}}}},2,1280,800);ui.draw();
    check(!ui.inspect()["elements"].contains("injected")&&!ui.inspect()["elements"]["score"]["visible"].get<bool>(),"UI text must remain text; visibility binding must apply");
    // A failed replacement must leave the previous context and file interface live.
    auto broken=authored;broken.sources["ui/hud.rml"]="<rml><head><link type='text/rcss' href='../../outside.rcss'/></head><body/></rml>";
    rejects([&]{lm::UiRenderer replacement(broken,executable,true);});ui.update(state,3,1280,800);ui.draw();
    auto duplicate=authored;duplicate.sources["ui/hud.rml"]="<rml><head/><body><div id='same'/><div id='same'/></body></rml>";
    rejects([&]{lm::UiRenderer invalid(duplicate,executable,true);});
    auto image=authored;image.sources["ui/hud.rml"]="<rml><head/><body><img src='unregistered.png'/></body></rml>";
    rejects([&]{lm::UiRenderer invalid(image,executable,true);});
    lm::UiRenderer second(authored,executable,true);second.update(state,4,960,600);second.draw();ui.update(state,4,1280,800);ui.draw();
    // Replacing a container must remove descendant IDs before their pointers die.
    ui.update({{"ui",{{"panel","replacement"},{"score",7}}}},5,1280,800);ui.draw();check(!ui.inspect()["elements"].contains("score"),"Removed descendants must not leave stale bindings");
}
void density_and_clipping(const std::filesystem::path& executable){
    auto authored=project();
    authored.sources["ui/main.rcss"]="body {width:100%;height:100%;} #panel {position:absolute;left:10dp;top:10dp;width:100dp;height:40dp;overflow:hidden;} button {display:block;width:180dp;height:30dp;}";
    authored.sources["ui/hud.rml"]="<rml><head><link type='text/rcss' href='main.rcss'/></head><body><div id='panel'><button id='wide'>Wide</button></div></body></rml>";
    lm::UiRenderer ui(authored,executable,true);ui.update(Json::object(),0,800,600,1);ui.draw();auto one=ui.inspect();
    ui.update(Json::object(),0,800,600,2);ui.draw();auto two=ui.inspect();
    check(std::abs(two["elements"]["panel"]["size"][0].get<float>()-2*one["elements"]["panel"]["size"][0].get<float>())<1,"dp width must scale with display density");
    check(std::abs(two["elements"]["panel"]["position"][0].get<float>()-20)<1&&two["density"]==2,"dp offset or inspection density incorrect");
    auto click=[&](float x,float y){SDL_Event event{};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.x=x;event.motion.y=y;ui.process_event(event);event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;event.button.button=SDL_BUTTON_LEFT;ui.process_event(event);event.type=SDL_EVENT_MOUSE_BUTTON_UP;ui.process_event(event);};
    click(300,40);check(ui.drain_events().empty(),"Clipped button pixels must not receive pointer activation");
    click(50,40);auto events=ui.drain_events();check(events.size()==1&&events[0]["id"]=="wide"&&ui.captures_keyboard(),"Scaled visible button must consume pointer and focus input");
    rejects([&]{ui.update(Json::object(),0,800,600,0);});
}
void safe_area(const std::filesystem::path& executable){
    auto authored=project();authored.sources["ui/main.rcss"]="body {width:100%;height:100%;} #panel {position:absolute;right:10dp;top:10dp;width:100dp;height:40dp;overflow:hidden;} button {display:block;width:180dp;height:30dp;}";
    authored.sources["ui/hud.rml"]="<rml><head><link type='text/rcss' href='main.rcss'/></head><body><div id='panel'><button id='wide'>Wide</button></div></body></rml>";
    lm::UiRenderer ui(authored,executable,true);ui.set_safe_area({80,30,650,500});ui.update(Json::object(),0,800,600,2);ui.draw();auto state=ui.inspect();
    check(state["viewport"]==Json::array({800,600})&&state["insets"]==Json::array({80,30,70,70}),"Insets must retain the full physical capture viewport");
    check(std::abs(state["elements"]["panel"]["position"][0].get<float>()-510)<1&&std::abs(state["elements"]["panel"]["position"][1].get<float>()-50)<1,"HUD anchoring must use safe dimensions and expose physical positions");
    auto click=[&](float x,float y){SDL_Event event{};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.x=x;event.motion.y=y;ui.process_event(event);event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;event.button.button=SDL_BUTTON_LEFT;ui.process_event(event);event.type=SDL_EVENT_MOUSE_BUTTON_UP;ui.process_event(event);};
    click(460,40);check(ui.drain_events().empty(),"Untranslated layout coordinates must not activate inset controls");
    click(750,65);check(ui.drain_events().empty(),"Safe-area and ancestor clipping must reject offscreen button pixels");
    click(530,65);auto events=ui.drain_events();check(events.size()==1&&events[0]["id"]=="wide","Physical pointer must map into inset HUD coordinates");
    ui.set_safe_area({20,60,740,480});ui.update(Json::object(),0,800,600,2);ui.draw();check(std::abs(ui.inspect()["elements"]["panel"]["position"][0].get<float>()-540)<1,"Changing safe area must relayout HUD anchors");
    ui.set_safe_area({});ui.update(Json::object(),0,800,600,2);check(ui.inspect()["insets"]==Json::array({0,0,0,0}),"Clearing safe area must restore desktop layout");
    rejects([&]{ui.set_safe_area({-1,0,200,200});});ui.set_safe_area({0,0,900,600});rejects([&]{ui.update(Json::object(),0,800,600);});
}
void authored_actions(const std::filesystem::path& executable){
    auto p=project();p.document={{"input",{{"actions",{{"dash",Json::object()}}}}}};
    p.sources["ui/main.rcss"]="body {width:100%;height:100%;} button {position:absolute;top:100dp;width:50dp;height:50dp;} #fire {left:100dp;} #dash {left:180dp;} button:active {background-color:#ffffff;}";
    p.sources["ui/hud.rml"]="<rml><head><link type='text/rcss' href='main.rcss'/></head><body><div id='controls'><button id='fire' data-action='fire' data-hit-padding='12'><span>Fire</span></button><button id='dash' data-action='dash'>Dash</button></div></body></rml>";
    lm::UiRenderer ui(p,executable,true);ui.update(Json::object(),0,800,600);
    using Phase=lm::UiRenderer::PointerPhase;
    auto input=[&]{lm::Input value;ui.apply(value);return value;};
    check(ui.process_pointer(1,Phase::down,90,120)&&input().fire,"Expanded hit region must activate an action through decorative descendants");
    check(!ui.captures_keyboard(),"Action focus must not suppress simultaneous gameplay bindings");
    check(ui.consumes_key(SDL_SCANCODE_SPACE)&&!ui.consumes_key(SDL_SCANCODE_D)&&ui.consumes_gamepad_button(SDL_GAMEPAD_BUTTON_SOUTH),"UI activation must consume only its activation keys/buttons");
    ui.consume_tick();check(input().fire,"Held action must survive pulse consumption");
    check(ui.process_pointer(2,Phase::down,200,120)&&input().fire&&input().actions["dash"]==1,"Independent custom pointers must hold separate actions");
    ui.consume_tick();ui.process_pointer(1,Phase::move,40,120);check(!input().fire&&input().actions["dash"]==1,"Drag-out must stop only the owning action");
    ui.process_pointer(1,Phase::move,120,120);check(input().fire,"Drag reentry must resume the original control");
    ui.process_pointer(1,Phase::cancel,120,120);check(!input().fire&&input().actions["dash"]==1,"Cancel must stop its action without clearing another finger");
    ui.update({{"ui",{{"dash",{{"opacity",0}}}}}},0,800,600);check(input().actions["dash"]==1,"Opacity zero must not disable a hit region");
    ui.update({{"ui",{{"dash",{{"disabled",true}}}}}},0,800,600);check(input().actions["dash"]==0&&ui.inspect()["owned_pointers"]==0,"Disabling UI must cancel all of its held sources");
    auto events=ui.drain_events();for(const auto& event:events)check(event["event"]!="click","Action binding must not generate an implicit duplicate click");
    ui.process_pointer(3,Phase::down,120,120);ui.process_pointer(3,Phase::up,120,120);check(input().fire,"A quick action tap must survive until the next tick");ui.consume_tick();check(!input().fire,"Quick taps must last only one consumed tick");
    ui.process_pointer(4,Phase::down,120,120);ui.update({{"ui",{{"fire",{{"visible",false}}}}}},0,800,600);check(!input().fire,"Hiding a control must cancel it");
    ui.update({{"ui",{{"fire",{{"visible",true}}}}}},0,800,600);ui.process_pointer(5,Phase::down,120,120);SDL_Event event{};event.type=SDL_EVENT_WINDOW_FOCUS_LOST;ui.process_event(event);check(!input().fire,"Window focus loss must clear transient actions");
    event.type=SDL_EVENT_KEY_DOWN;event.key.key=SDLK_ESCAPE;ui.process_event(event);event.key.key=SDLK_TAB;ui.process_event(event);event.key.key=SDLK_SPACE;ui.process_event(event);check(input().fire,"Keyboard activation must hold the focused action");ui.consume_tick();event.type=SDL_EVENT_KEY_UP;ui.process_event(event);check(!input().fire,"Keyboard release must stop the focused action");
    event={};event.type=SDL_EVENT_GAMEPAD_BUTTON_DOWN;event.gbutton.button=SDL_GAMEPAD_BUTTON_SOUTH;ui.process_event(event);check(input().fire,"Gamepad accept must hold the focused action");ui.consume_tick();event.type=SDL_EVENT_GAMEPAD_BUTTON_UP;ui.process_event(event);check(!input().fire,"Gamepad release must stop its action");
    ui.process_pointer(6,Phase::down,120,120);ui.set_safe_area({10,10,780,580});check(!input().fire,"Safe-area changes must cancel stale hit ownership");
    ui.update(Json::object(),0,800,600);event={};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.x=130;event.motion.y=130;ui.process_event(event);event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;event.button.button=SDL_BUTTON_LEFT;ui.process_event(event);check(input().fire,"Mouse press must use the same action path");ui.consume_tick();event.type=SDL_EVENT_MOUSE_BUTTON_UP;ui.process_event(event);check(!input().fire,"Mouse release must stop its held action");
    check(ui.diagnostics().empty(),"Valid action UI should have no layout/action/font warnings");
    ui.process_pointer(7,Phase::down,130,130);ui.update({{"ui",{{"controls","Removed controls"}}}},0,800,600);check(!input().fire&&!ui.inspect()["elements"].contains("fire"),"Removing an owning control must cancel before its element pointer becomes stale");
    auto bad=p;bad.sources["ui/hud.rml"]="<rml><head/><body><button data-action='fire'>Missing id</button></body></rml>";rejects([&]{lm::UiRenderer invalid(bad,executable,true);});
}
void layout_diagnostics(const std::filesystem::path& executable){
    auto p=project();p.ui["expected_characters"]="\xF4\x8F\xBF\xBF";
    p.sources["ui/main.rcss"]="body {width:100%;height:100%;} button {position:absolute;left:10dp;top:10dp;width:50dp;height:40dp;} #off {left:390dp;}";
    p.sources["ui/hud.rml"]="<rml><head><link type='text/rcss' href='main.rcss'/></head><body><button id='one'>One</button><button id='two' data-action='unregistered'>Two</button><button id='off'>Offscreen</button></body></rml>";
    lm::UiRenderer ui(p,executable,true);ui.set_safe_area({20,10,760,580});ui.update(Json::object(),0,800,600,2);
    const auto state=ui.inspect();check(state["elements"]["one"]["logical_position"]==Json::array({10,10})&&state["logical_content_size"]==Json::array({380,290}),"Inspection must distinguish logical layout from physical bounds");
    std::set<std::string> codes;for(const auto& diagnostic:ui.diagnostics())codes.insert(diagnostic["code"]);
    check(codes.contains("ui_missing_glyph")&&codes.contains("ui_action")&&codes.contains("ui_overlap")&&codes.contains("ui_offscreen"),"UI diagnostics must report missing glyphs, action references, overlap and offscreen controls");
}
}

void pointer_only_gameplay(const std::filesystem::path& executable){
    auto p=project();p.document["input"]={{"actions",{{"cast",Json::object()}}}};
    p.sources["ui/main.rcss"]="body {width:100%;height:100%;} #cast-area {position:absolute;left:100dp;top:100dp;width:300dp;height:300dp;focus:none;} #spell {position:absolute;left:10dp;top:10dp;width:80dp;height:40dp;focus:none;}";
    p.sources["ui/hud.rml"]="<rml><head><link type='text/rcss' href='main.rcss'/></head><body><button id='spell'>Water</button><div id='cast-area' data-action='cast'/></body></rml>";
    lm::UiRenderer ui(p,executable,true);ui.update(Json::object(),0,800,600);ui.draw();
    auto click=[&](float x,float y){SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=x;e.motion.y=y;ui.process_event(e);e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.button=SDL_BUTTON_LEFT;ui.process_event(e);e.type=SDL_EVENT_MOUSE_BUTTON_UP;ui.process_event(e);ui.consume_tick();ui.drain_events();};
    for(auto xy:{std::pair{150.f,150.f},std::pair{30.f,30.f}}){
        click(xy.first,xy.second);
        check(!ui.captures_keyboard()&&!ui.consumes_key(SDL_SCANCODE_SPACE),"Gameplay click must leave Space to jump");
        SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.key=SDLK_SPACE;e.key.scancode=SDL_SCANCODE_SPACE;ui.process_event(e);
        lm::Input input;ui.apply(input);
        check(input.actions["cast"]==0&&ui.drain_events().empty(),"Space after pointer release must not cast or activate a button");
        e.type=SDL_EVENT_KEY_UP;ui.process_event(e);
    }
}

void drag_scroll(const std::filesystem::path& executable){
    auto p=project();
    p.sources["ui/main.rcss"]="body {width:100%;height:100%;} div {display:block;} #list {position:absolute;left:10dp;top:10dp;width:200dp;height:200dp;overflow-y:auto;} #rows {height:900dp;} scrollbarvertical {width:16dp;} #exit {position:absolute;left:10dp;top:220dp;width:200dp;height:40dp;}";
    p.sources["ui/hud.rml"]="<rml><head><link type='text/rcss' href='main.rcss'/></head><body><div id='list' data-drag-scroll='true'><div id='rows'>Discoveries</div></div><button id='exit'>Back</button></body></rml>";
    lm::UiRenderer ui(p,executable,true);ui.set_safe_area({0,30,600,840});ui.update({{"ui",Json::object()}},0,600,900,2);ui.draw();
    using Phase=lm::UiRenderer::PointerPhase;
    const float before=ui.inspect()["elements"]["rows"]["position"][1];
    check(ui.process_pointer(40,Phase::down,100,350),"List must capture drag on content");
    check(ui.process_pointer(40,Phase::move,100,150),"List must retain drag");
    ui.update({{"ui",Json::object()}},.1,600,900,2);ui.draw();
    const auto after=ui.inspect()["elements"]["rows"]["position"][1].get<float>();
    if(!(after<before-190))std::cerr<<"scroll before="<<before<<" after="<<after<<" list="<<ui.inspect()["elements"]["list"]<<'\n';
    check(after<before-190,"Content must scroll with finger at device density and safe area");
    check(ui.process_pointer(40,Phase::up,100,150),"List release must be consumed");
    check(ui.drain_events().empty(),"Dragging must not click a game control");
    check(!ui.process_pointer(41,Phase::down,100,500),"Back button must not become a scroll drag");
    ui.process_pointer(42,Phase::down,100,200);ui.reset_input();
    check(!ui.process_pointer(42,Phase::move,100,100),"Reset must cancel scroll ownership");
    ui.process_pointer(43,Phase::down,100,200);ui.update({{"ui",{{"list",{{"visible",false}}}}}},.2,600,900,2);
    check(!ui.process_pointer(43,Phase::move,100,100),"Hidden notebook must release drag ownership");
}
int main(int argc,char** argv){try{const auto root=argc>1?std::filesystem::path(argv[1]):std::filesystem::current_path();const auto executable=argc>2?std::filesystem::path(argv[2]):root/"build";layout_input(executable);density_and_clipping(executable);safe_area(executable);authored_actions(executable);layout_diagnostics(executable);drag_scroll(executable);pointer_only_gameplay(executable);std::cout<<"PASS RmlUi layout, safe area, controls, drag scrolling, cancellation and input tests\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
