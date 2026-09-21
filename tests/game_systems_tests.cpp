#include "langmake/world.hpp"
#include "langmake/navigation.hpp"
#include "langmake/project.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool okay,const std::string& message){++checks;if(!okay)throw std::runtime_error(message);}
void rejects(const std::function<void()>& run,const std::string& part){try{run();}catch(const std::exception& error){require(std::string(error.what()).find(part)!=std::string::npos,std::string("wrong error: ")+error.what());return;}throw std::runtime_error("Expected rejection: "+part);}
lm::Project sample(const std::string& dimension="3d"){
    lm::Project p;p.document=lm::Json::object();p.dimension=dimension;p.title="Systems fixture";p.revision="systems-v1";p.script_file="fixture.luau";p.script_source="return {}";
    lm::Entity floor;floor.id="floor";floor.position={0,-.5f,0};floor.size={40,1,40};floor.body="static";
    lm::Entity player;player.id="player";player.role="player";player.body="dynamic";player.position={0,3,0};player.size={1,2,1};player.shape="capsule";player.controller="character";
    p.entities={floor,player};return p;
}
lm::Json entity(const lm::World& world,const std::string& id){const auto snapshot=world.snapshot();for(const auto& e:snapshot["entities"])if(e["id"]==id)return e;throw std::runtime_error("Missing entity "+id);}
void viewport_observation(){
    auto p=sample();p.gravity={0,0,0};p.script_source=R"(return {
start=function() engine.state_set('initial_viewport',engine.viewport()) end,
update=function() local v=engine.viewport();engine.state_set('viewport',v);v.width=1;assert(engine.viewport().width~=1) end,
on_load=function() engine.state_set('loaded_viewport',engine.viewport()) end})";
    lm::World world(p);require(world.snapshot()["state"]["initial_viewport"]==lm::Json({{"available",false}}),"Initial start invented host viewport");
    require(!world.snapshot()["viewport"]["available"],"Headless world invented viewport");
    const auto entities=world.entities();world.set_viewport(1200,2400,3,{0,90,1200,2220});world.step(1.f/60,{});
    const auto viewport=world.snapshot()["viewport"];
    require(viewport["width"]==1200&&viewport["height"]==2400&&viewport["density"]==3&&viewport["logical_size"]==lm::Json({400,800}),"Viewport physical/logical units differ");
    require(viewport["safe_area"]==lm::Json({0,90,1200,2220})&&viewport["logical_safe_area"]==lm::Json({0,30,400,740}),"Viewport safe area units differ");
    require(world.snapshot()["state"]["viewport"]==viewport&&world.entities()[0].size==entities[0].size,"Viewport query changed geometry or failed to copy observation");
    rejects([&]{world.set_viewport(0,2400,3);},"Viewport");rejects([&]{world.set_viewport(1200,2400,0);},"Viewport");
    rejects([&]{world.set_viewport(1200,2400,3,{0,0,1201,2400});},"safe area");
    require(world.snapshot()["viewport"]==viewport,"Invalid viewport setter was not transactional");
    const auto saved=world.save();world.set_viewport(1600,900,2);world.restore(saved);
    const auto current=world.snapshot()["viewport"];
    require(current["width"]==1600&&current["safe_area"]==lm::Json({0,0,1600,900})&&world.snapshot()["state"]["loaded_viewport"]==current,"Save restore replaced current host viewport or on_load saw stale metrics");
    auto other=std::make_shared<lm::Project>(p);other->scene_id="other";p.scenes["other"]=other;
    lm::World scenes(p);scenes.set_viewport(900,1600,2);scenes.change_scene("other");
    require(scenes.snapshot()["state"]["initial_viewport"]["width"]==900,"Fresh scene start did not see current host viewport");
    scenes.change_scene("main");
    require(scenes.snapshot()["state"]["loaded_viewport"]["width"]==900,"Restored scene on_load lost current host viewport");
}
void save_presentation_compatibility(){
    auto p=sample();p.gravity={0,0,0};p.document["project_id"]="presentation-save";p.document["save_version"]=1;
    p.document["save_compatibility"]={{"id","progress-v1"},{"allow_presentation_changes",true}};
    p.document["input"]={{"actions",{{"shoot",{{"keys",{"Space"}}}}}}};
    p.document["game_camera"]={{"half_height",10}};p.ui={{"data",{{"layout","old"}}}};
    p.script_source=R"(return {
start=function() engine.state_set('coins',engine.state_get('coins',0)+5);engine.ui_set('stale','old') end,
on_load=function()
assert(not engine.input('fire') and not engine.input('jump'))
assert(engine.input('shoot')==0 and not engine.input_pressed('shoot') and not engine.input_released('shoot'))
engine.ui_set('loaded',true)
end,
update=function() if engine.input_released('fire') or engine.input_released('shoot') then engine.state_set('false_release',true) end end})";
    lm::World original(p);lm::Input held;held.fire=true;held.jump=true;held.actions["shoot"]=1;held.pointer={50,90};original.step(1.f/60,held);
    const auto saved=original.save();
    require(lm::save_compatibility_report(p,saved)["status"]=="exact","Exact save compatibility report incorrect");
    original.restore(saved);original.step(1.f/60,{});
    require(!original.snapshot()["state"].contains("false_release"),"Restore manufactured an input release edge");
    auto next=p;next.revision="presentation-v2";next.profile="phone";next.document["game_camera"]["half_height"]=20;
    next.document["application"]={{"label","New label"},{"build",2}};
    next.document["input"]["actions"]["shoot"]["keys"]={"Left Ctrl"};next.ui["data"]={{"layout","new"}};
    require(lm::save_compatibility_report(next,saved)["status"]=="presentation_compatible","Assessed presentation change requested migration");
    lm::World upgraded(next);upgraded.restore(saved);
    require(upgraded.snapshot()["state"]["coins"]==5,"Presentation reuse repeated initialization reward");
    require(upgraded.presentation()["ui"]["layout"]=="new"&&!upgraded.presentation()["ui"].contains("stale")&&upgraded.presentation()["ui"]["loaded"],"Old saved UI replaced current defaults or on_load did not run");
    require(upgraded.presentation()["camera"]["half_height"]==20,"Old saved camera resurrected during presentation reuse");
    for(int i=0;i<4;++i)upgraded.restore(saved);
    require(upgraded.snapshot()["state"]["coins"]==5,"Repeated compatible restore duplicated reward");
    const auto current_save=upgraded.save();require(lm::save_compatibility_report(next,current_save)["status"]=="exact","Reused save did not adopt current content identity");
    auto structural=next;structural.entities[1].size[0]+=1;
    require(lm::save_compatibility_report(structural,saved)["status"]=="migration_required","Collider change bypassed migration");
    structural=next;structural.script_source+="\n-- changed gameplay source";
    require(lm::save_compatibility_report(structural,saved)["status"]=="migration_required","Script change bypassed migration");
    structural=next;structural.document["input"]["actions"]["new_action"]=lm::Json::object();
    require(lm::save_compatibility_report(structural,saved)["status"]=="migration_required","Action set change bypassed migration");
    auto legacy=saved;legacy.erase("save_compatibility");
    require(lm::save_compatibility_report(next,legacy)["status"]=="migration_required","Legacy opt-out save silently upgraded");
    auto opt_out=next;opt_out.document.erase("save_compatibility");
    require(lm::save_compatibility_report(opt_out,saved)["status"]=="migration_required","Current opt-out contract ignored");
    const auto before=upgraded.save();rejects([&]{upgraded.restore(legacy);},"migrate");
    require(upgraded.save()==before&&saved["input"]["fire"]==true,"Failed migration changed live state or caller save bytes");
    auto migration=next;migration.document["save_version"]=2;migration.revision="migration-v3";
    migration.script_source=R"(return {start=function() engine.state_set('coins',999) end,
migrate=function(s,from,to) assert(from==1 and to==2);s.state.capacity=(s.state.capacity or 10)+5;return s end,
on_load=function() assert(not engine.input('fire'));assert(engine.state_get('coins')==5) end})";
    lm::World migrated(migration);migrated.restore(saved);const auto migrated_save=migrated.save();
    migrated.restore(migrated_save);migrated.restore(saved);
    require(migrated.snapshot()["state"]["capacity"]==15&&migrated.snapshot()["state"]["coins"]==5,"Repeated migration/load duplicated capacity or initialization reward");
    auto scenes=p;auto main=std::make_shared<lm::Project>(p),other=std::make_shared<lm::Project>(p);other->scene_id="other";
    scenes.scenes={{"main",main},{"other",other}};
    lm::World first_session(scenes);first_session.change_scene("other");const auto scene_save=first_session.save();
    auto target=scenes;target.revision="scene-presentation-v2";target.ui["data"]={{"layout","current-main"}};
    for(auto&[id,scene]:target.scenes){scene=std::make_shared<lm::Project>(*scene);scene->revision=target.revision;scene->ui["data"]={{"layout","current-"+id}};}
    lm::World resumed(target);resumed.restore(scene_save);
    require(resumed.current_project().scene_id=="other"&&resumed.presentation()["ui"]["layout"]=="current-other","Cross-profile restore did not refresh active scene UI");
    resumed.change_scene("main");
    require(resumed.presentation()["ui"]["layout"]=="current-main"&&resumed.snapshot()["state"]["coins"]==10,"Stored scene kept stale UI or repeated initialization rewards");
}
void modules_and_state(){auto p=sample();p.modules["shared"]=R"(local M={};function M.add(n) engine.state_set('visits',engine.state_get('visits',0)+n) end;return M)";
    p.modules["wrapper"]=R"(return {shared=require('shared')})";
    p.script_source=R"(local a=require('shared');local b=require('wrapper');assert(a==b.shared)
return {start=function() a.add(2);engine.state_set('nested',{a={1,2,3},flag=true});local copy=engine.state_get('nested');copy.a[1]=999;assert(engine.state_get('nested').a[1]==1)
local cycle={};cycle.self=cycle;assert(not pcall(function() engine.state_set('bad',cycle) end));assert(engine.state_get('bad')==nil) end,
update=function() engine.state_set('pointer',{engine.input('pointer_x'),engine.input('pointer_y')});if engine.input_pressed('interact') then a.add(1) end end})";
    lm::World w(p);require(w.snapshot()["state"]["visits"]==2,"module must share cached table and declared state");lm::Input input;input.actions["interact"]=1;
    input.pointer={123,456};w.step(1.f/60,input);w.step(1.f/60,input);require(w.snapshot()["state"]["visits"]==3,"button edge repeats while held");
    require(w.snapshot()["state"]["pointer"]==lm::Json({123,456}),"pointer coordinates did not reach script input");
    w.step(1.f/60,{});w.step(1.f/60,input);require(w.snapshot()["state"]["visits"]==4,"button edge lost after release");
    p.modules["a"]="return require('b')";p.modules["b"]="return require('a')";p.script_source="return require('a')";
    rejects([&]{lm::World bad(p);},"module dependency cycle");
    auto seeded=sample();seeded.script_source="return {start=function() engine.state_set('random',{engine.random(),engine.random(),engine.random()}) end}";
    seeded.document["seed"]=42;lm::World first(seeded),repeat(seeded);seeded.document["seed"]=43;lm::World different(seeded);
    require(first.snapshot()["state"]==repeat.snapshot()["state"],"equal authored seeds produced different random streams");
    require(first.snapshot()["state"]!=different.snapshot()["state"],"authored seed did not change random stream");
    seeded.document["seed"]=0;lm::World zero(seeded);seeded.document.erase("seed");lm::World fallback(seeded);
    require(zero.snapshot()["state"]==fallback.snapshot()["state"],"zero seed did not select nonzero engine default");
    seeded.document["seed"]=-1;rejects([&]{lm::World invalid(seeded);},"seed");
    seeded.document["seed"]=9007199254740992ull;rejects([&]{lm::World invalid(seeded);},"seed");
}
void lifetime_and_queries(const std::string& dimension){auto p=sample(dimension);p.gravity={0,0,0};p.entities[1].controller.clear();p.entities[1].shape="box";p.entities[1].size={1,1,1};p.entities[1].position={0,0,0};
    p.entities[0].body="none";
    lm::Entity wall;wall.id="wall";wall.position={5,0,0};wall.size={1,2,2};p.entities.push_back(wall);
    p.prefabs["shot"]={{"body","dynamic"},{"position",{1,0,0}},{"size",{.2,.2,.2}},{"shape","sphere"},{"role","projectile"}};
    p.script_source=R"(return {start=function()
local id=engine.spawn('shot');engine.state_set('shot',id);assert(engine.exists(id));engine.set_velocity(id,4,0,0);engine.set_visual_scale(id,2,3,4);engine.set_opacity(id,.25)
local hit=engine.raycast(1,0,0,8,0,0,{exclude={'player'},limit=1});assert(#hit.hits==1 and hit.hits[1].id=='wall')
local nearby=engine.overlap(5,0,0,1);assert(#nearby.entities==1 and nearby.entities[1]=='wall')
local swept=engine.shape_cast(1,0,0,8,0,0,{radius=.2,exclude={'player'},limit=1});assert(swept.hits[1].id=='wall')
engine.set_visible('wall',false);assert(#engine.overlap(5,0,0,1).entities==1)
engine.set_collision_enabled('wall',false);assert(#engine.overlap(5,0,0,1).entities==0)
end,update=function() if engine.input('fire') then local id=engine.state_get('shot');engine.destroy(id);assert(not engine.exists(id)) end end})";
    lm::World w(p);const std::string id=w.snapshot()["state"]["shot"];w.step(1.f/60,{});auto shot=entity(w,id);
    require(shot["position"][0].get<float>()>1.04f,"spawned dynamic body did not move");require(shot["visual_scale"]==lm::Json({2,3,4}),"visual scale not observable");
    require(std::abs(shot["color"][3].get<float>()-.25f)<.001f,"opacity not observable");
    lm::Input input;input.fire=true;w.step(1.f/60,input);require(w.snapshot()["entities"].size()==3,"despawn did not release entity");
    p.script_source=R"(return {update=function() local previous=engine.state_get('last');if previous then engine.destroy(previous) end;engine.state_set('last',engine.spawn('shot')) end})";
    lm::World churn(p);for(int i=0;i<1000;++i)churn.step(1.f/60,{});require(churn.snapshot()["entities"].size()==4,"spawn churn leaked active entities");
    require(churn.snapshot()["state"]["last"]=="runtime_1000","runtime identity sequence must not reuse dead IDs");
    require(churn.statistics()["physics_bodies"]==3&&churn.statistics()["runtime_slots"].get<int>()<=5,"spawn churn leaked physics bodies or record slots");
    p.script_source="return {start=function() engine.set_velocity('player',3,0,0);engine.set_collision_enabled('player',false);engine.set_position('player',5,0,0) end}";
    lm::World teleported(p);teleported.step(1.f/60,{});
    require(std::abs(entity(teleported,"player")["position"][0].get<float>()-5)<.0001f&&entity(teleported,"player")["velocity"]==lm::Json({0,0,0}),dimension+" disabled-body teleport retained velocity");
}
void characters(const std::string& dimension){auto p=sample(dimension);p.script_source=R"(return {update=function() engine.move_player(engine.input('move_x')*4,0);if engine.input_pressed('jump') then engine.jump(6) end end})";
    lm::World w(p);for(int i=0;i<180;++i)w.step(1.f/60,{});auto position=entity(w,"player")["position"];
    require(position[1].get<float>()>.9f&&position[1].get<float>()<1.2f,dimension+" capsule did not rest on floor");require(w.snapshot()["grounded"],dimension+" capsule did not report grounded");
    lm::Input jump;jump.jump=true;for(int i=0;i<12;++i)w.step(1.f/60,jump);
    require(entity(w,"player")["position"][1].get<float>()>1.5f,dimension+" capsule did not jump");
    for(int i=0;i<150;++i)w.step(1.f/60,{});require(w.snapshot()["grounded"],dimension+" capsule did not return to ground");
    p.entities[1].position={0,1.1f,0};lm::Entity ceiling;ceiling.id="ceiling";ceiling.position={0,3,0};ceiling.size={10,1,10};p.entities.push_back(ceiling);
    lm::Entity wall;wall.id="wall";wall.position={3,2,0};wall.size={1,4,10};p.entities.push_back(wall);
    lm::World constrained(p);for(int i=0;i<60;++i)constrained.step(1.f/60,{});float maximum=0;
    for(int i=0;i<45;++i){constrained.step(1.f/60,jump);maximum=std::max(maximum,entity(constrained,"player")["position"][1].get<float>());}
    require(maximum<1.65f,dimension+" character tunneled through ceiling");
    lm::Input move;move.move_x=1;for(int i=0;i<180;++i)constrained.step(1.f/60,move);
    require(entity(constrained,"player")["position"][0].get<float>()<2.1f,dimension+" character crossed wall");
    auto steps=sample(dimension);steps.entities[1].position={0,1.1f,0};steps.script_source=p.script_source;
    lm::Entity step;step.id="step";step.position={2,.125f,0};step.size={2,.25f,8};steps.entities.push_back(step);
    lm::World stairs(steps);for(int i=0;i<60;++i)stairs.step(1.f/60,{});float highest=0;
    for(int i=0;i<100;++i){stairs.step(1.f/60,move);highest=std::max(highest,entity(stairs,"player")["position"][1].get<float>());}
    require(entity(stairs,"player")["position"][0].get<float>()>3.5f&&highest>1.15f,dimension+" controller did not negotiate low step");
    auto disabled=sample(dimension);disabled.script_source=R"(return {update=function() if engine.input('fire') then engine.set_collision_enabled('player',false);engine.state_set('jump_allowed',engine.jump(6)) end end})";
    lm::World ghost(disabled);for(int i=0;i<180;++i)ghost.step(1.f/60,{});require(ghost.snapshot()["grounded"],dimension+" disabled-controller fixture failed to settle");
    lm::Input disable;disable.fire=true;ghost.step(1.f/60,disable);
    require(!ghost.snapshot()["grounded"]&&!ghost.snapshot()["state"]["jump_allowed"].get<bool>(),dimension+" disabled controller retained support/jump eligibility");
}
void save_scene_camera(){auto p=sample();p.gravity={0,0,0};p.entities[1].controller.clear();p.document["game_camera"]={{"follow","player"},{"offset",{0,4,8}},{"obstruction",false}};
    p.script_source=R"(return {start=function() engine.state_set('starts',engine.state_get('starts',0)+1);engine.state_set('counter',0) end,
update=function() engine.state_set('counter',engine.state_get('counter')+1);engine.state_set('random',engine.random());engine.set_velocity('player',1,0,0) end,
on_load=function() engine.ui_set('loaded',true) end})";
    auto other=std::make_shared<lm::Project>(p);other->scene_id="other";other->script_source="return {start=function() engine.state_set('other_started',true) end}";
    auto main=std::make_shared<lm::Project>(p);p.scenes={{"main",main},{"other",other}};
    lm::World w(p);for(int i=0;i<30;++i)w.step(1.f/60,{});auto saved=w.save();auto old_position=entity(w,"player")["position"];
    w.step(1.f/60,{});const auto next_random=w.snapshot()["state"]["random"];
    lm::World loaded(p);loaded.restore(saved);require(loaded.snapshot()["state"]["counter"]==30,"save restored start defaults instead of declared state");
    require(entity(loaded,"player")["position"]==old_position,"save lost entity transform");loaded.step(1.f/60,{});
    require(loaded.snapshot()["state"]["random"]==next_random,"RNG continuation changed across save");require(loaded.presentation()["camera"]["enabled"],"camera absent from lightweight presentation");
    auto damaged=saved;damaged["source_revision"]="wrong";auto before=loaded.snapshot()["state"];
    rejects([&]{loaded.restore(damaged);},"migrate");require(loaded.snapshot()["state"]==before,"failed load damaged live state");
    loaded.change_scene("other");require(loaded.snapshot()["state"]["counter"]==31&&loaded.snapshot()["state"]["other_started"],"scene transition lost global state or failed to start scene");
    loaded.change_scene("main");require(loaded.snapshot()["state"]["starts"]==1,"scene revisit repeated start rewards");require(loaded.snapshot()["tick"]==31,"scene transition reset global timeline");
    const auto serial=loaded.audio_events().back()["serial"].get<std::uint64_t>();loaded.change_scene("other");
    require(loaded.audio_events().back()["serial"].get<std::uint64_t>()>serial,"scene transition restarted audio serial numbers");
    auto malformed=saved;malformed["camera"]["position"]={0,0,0};malformed["camera"]["target"]={0,0,0};
    rejects([&]{w.restore(malformed);},"must differ");
    malformed=saved;malformed["entities"][0]["size"]={0,0,0};rejects([&]{w.restore(malformed);},"size must");
    require(w.snapshot()["state"]["counter"]==31,"invalid save altered current live state");
    auto old=sample();old.document["project_id"]="migration-test";old.script_source="return {start=function() engine.state_set('coins',3) end}";
    lm::World original(old);auto old_save=original.save();auto revised=old;revised.revision="systems-v2";revised.document["save_version"]=2;
    revised.script_source=R"(return {migrate=function(saved,from,to) assert(from==1 and to==2);saved.state.gems=saved.state.coins*2;saved.state.coins=nil;return saved end,
on_load=function() engine.state_set('loaded_migration',true) end})";
    lm::World upgraded(revised);upgraded.restore(old_save);require(upgraded.snapshot()["state"]["gems"]==6&&upgraded.snapshot()["state"]["loaded_migration"],"authored save migration did not run transactionally");
    old_save["project_id"]="different-game";rejects([&]{upgraded.restore(old_save);},"another project");
    for(int i=0;i<40;++i){loaded.restore(saved);loaded.change_scene("other");loaded.change_scene("main");}
    require(loaded.statistics()["physics_bodies"]==2&&loaded.statistics()["entities"]==2,"repeated load/scene changes leaked runtime resources");
    const auto live=loaded.snapshot();loaded.set_scene_prepare([](const lm::Project&,const lm::CompiledAssets&){throw std::runtime_error("presentation allocation failed");});
    rejects([&]{loaded.change_scene("other");},"presentation allocation");require(loaded.snapshot()==live,"presentation failure partially committed scene transition");
    rejects([&]{loaded.restore(saved);},"presentation allocation");require(loaded.snapshot()==live,"presentation failure partially committed player save");
    auto bounded=sample();bounded.entities.clear();for(int i=1;i<=65;++i){auto child=std::make_shared<lm::Project>(bounded);child->scenes.clear();child->scene_id="scene"+std::to_string(i);bounded.scenes[child->scene_id]=child;}
    lm::World many(bounded);for(int i=1;i<=63;++i)many.change_scene("scene"+std::to_string(i));const auto safe=many.save();
    rejects([&]{many.change_scene("scene64");},"64 saved scenes");require(many.save()==safe,"scene capacity error mutated saveable session");
    many.change_scene("main");many.restore(safe);require(many.current_project().scene_id=="scene63","visited-scene capacity prevented revisiting or restoring a valid scene");
}
void camera_and_contacts(){auto p=sample();p.gravity={0,0,0};p.entities[1].position={0,2,0};p.entities[1].controller.clear();
    lm::Entity target;target.id="target";target.body="none";target.position={0,2,-5};p.entities.push_back(target);
    lm::Entity wall;wall.id="camera_wall";wall.position={0,4,4};wall.size={10,8,1};p.entities.push_back(wall);
    p.script_source=R"(return {start=function() engine.camera_set({mode='lock_on',follow='player',target_entity='target',offset={0,3,8},smoothing=0}) end,
update=function() if engine.input('fire') then engine.destroy('target') end end})";
    lm::World w(p);w.step(1.f/60,{});auto camera=w.presentation()["camera"];
    require(camera["target_locked"]&&camera["occluded"],"target lock or camera obstruction was not resolved");
    require(camera["position"][2].get<float>()<3.5f,"camera clipped through obstruction");
    require(std::abs(camera["target"][2].get<float>()+2.5f)<.001f,"lock-on camera must frame both actors");
    lm::Input input;input.fire=true;w.step(1.f/60,input);require(!w.presentation()["camera"]["target_locked"],"destroyed target left stale camera lock");
    p.entities.back().position={20,4,4};p.script_source=R"(return {start=function() engine.camera_set({mode='follow',follow='player',offset={0,3,8},position={0,5,8},smoothing=.1}) end,
update=function() if engine.input('fire') then engine.set_position('camera_wall',0,4,4) end end})";
    lm::World moving_wall(p);moving_wall.step(1.f/60,{});moving_wall.step(1.f/60,input);
    require(moving_wall.presentation()["camera"]["position"][2].get<float>()<3.5f,"smoothed camera remained behind newly moved obstruction");
    p.script_source=R"(return {start=function() engine.camera_set({mode='follow',follow='player'}) end,
update=function() engine.camera_set({mode='fixed',position={10,11,12},target={1,2,3}}) end})";
    lm::World fixed(p);fixed.step(1.f/60,{});
    require(fixed.presentation()["camera"]["position"]==lm::Json({10,11,12})&&fixed.presentation()["camera"]["target"]==lm::Json({1,2,3}),"fixed camera mode continued following an old target");
    auto contact=sample("2d");contact.entities[1].controller.clear();contact.entities[1].shape="box";contact.script_source="return {}";
    lm::World physical(contact);for(int i=0;i<180;++i)physical.step(1.f/60,{});
    require(!physical.snapshot()["contacts"].empty(),"native contact events were not exposed");auto saved=physical.save();physical.restore(saved);physical.step(1.f/60,{});
    require(physical.snapshot()["contacts"][0]["phase"]=="stay","save/load replayed a previously active contact as enter");
}
void moving_support(const std::string& dimension){auto p=sample(dimension);p.entities[0].body="none";
    lm::Entity platform;platform.id="platform";platform.body="kinematic";platform.position={0,0,0};platform.size={8,1,8};p.entities.push_back(platform);
    p.entities[1].position={0,2,0};p.script_source=R"(return {update=function() engine.move_player(0,0);engine.set_velocity('platform',engine.input('fire') and 1 or 0,0,0) end})";
    lm::World w(p);for(int i=0;i<90;++i)w.step(1.f/60,{});require(w.snapshot()["grounded"],dimension+" character failed to settle on kinematic support");
    const float start=entity(w,"player")["position"][0];lm::Input move;move.fire=true;for(int i=0;i<120;++i)w.step(1.f/60,move);
    const float distance=entity(w,"player")["position"][0].get<float>()-start;
    require(distance>1.8f&&distance<2.2f,dimension+" character did not inherit support velocity: "+std::to_string(distance));
    require(w.snapshot()["grounded"],dimension+" moving platform lost grounded state");
    const auto saved=w.save();w.restore(saved);for(int i=0;i<30;++i)w.step(1.f/60,move);
    require(entity(w,"player")["position"][0].get<float>()>start+2.3f,dimension+" saved controller lost moving support intent");
}
void joint_attachments(){auto p=sample();p.entities.clear();p.gravity={0,0,0};lm::AssetSource model;model.type="model";model.revision="joint-attachment-v1";
    model.document=lm::Json::parse(R"({"schema_version":1,"joints":[{"id":"hand"}],"parts":[{"id":"hand","joint":"hand","color":"#AABBCC","geometry":{"op":"box","size":[1,1,1]}}],
"animations":{"wave":{"duration":1,"loop":false,"tracks":{"hand":{"translation":[{"time":0,"value":[0,0,0]},{"time":1,"value":[0,2,0]}],"rotation":[{"time":0,"value":[0,0,0,1]},{"time":1,"value":[0,0,1,0]}]}}}},"default_animation":"wave"})");p.assets["rig"]=model;
    lm::Entity parent;parent.id="parent";parent.body="none";parent.asset="rig";parent.animation="wave";p.entities.push_back(parent);
    lm::Entity sword;sword.id="sword";sword.body="none";sword.parent="parent";sword.parent_joint="hand";sword.position={1,0,0};p.entities.push_back(sword);
    auto second=std::make_shared<lm::Project>(p);second->scene_id="second";p.scenes["second"]=second;
    lm::World w(p);require(std::abs(entity(w,"sword")["position"][0].get<float>()-1)<.01f,"joint attachment initial offset incorrect");
    for(int i=0;i<30;++i)w.step(1.f/60,{});const auto attached=entity(w,"sword");
    require(std::abs(attached["position"][0].get<float>())<.02f&&std::abs(attached["position"][1].get<float>()-2)<.02f,"attachment did not follow animated joint translation and rotation in same tick");
    require(std::abs(std::abs(attached["rotation"][2].get<float>())-90)<.1f,"attachment did not inherit animated joint orientation");
    const auto saved=w.save();w.restore(saved);require(entity(w,"sword")["position"]==attached["position"],"save/load changed joint attachment transform");
    w.change_scene("second");require(std::abs(entity(w,"sword")["position"][0].get<float>()-1)<.01f&&std::abs(entity(w,"sword")["position"][1].get<float>())<.01f,"fresh scene animation started at global session time");
    p.entities[1].parent_joint="missing";rejects([&]{lm::World invalid(p);},"joint");
}
void hierarchy_and_sensors(const std::string& dimension){auto p=sample(dimension);p.gravity={0,0,0};p.entities[0].body="none";p.entities[1].controller.clear();p.entities[1].position={0,0,0};p.entities[1].shape="box";p.entities[1].size={.5,.5,.5};
    lm::Entity sensor;sensor.id="sensor";sensor.body="trigger";sensor.position={3,0,0};sensor.size={1,2,2};p.entities.push_back(sensor);
    p.prefabs["rig"]={{"entities",lm::Json::array({
        {{"id","root"},{"body","kinematic"},{"position",{0,5,0}},{"size",{1,1,1}}},
        {{"id","arm"},{"parent","root"},{"body","none"},{"position",{1,0,0}},{"size",{.2,.2,.2}}},
        {{"id","tip"},{"parent","arm"},{"body","none"},{"position",{1,0,0}},{"size",{.1,.1,.1}}}})}};
    p.script_source=R"(return {start=function()
local rig=engine.spawn('rig',{position={3,0,0},entities={tip={color={1,0,0,1}}}});engine.state_set('rig',rig);engine.set_velocity(rig,1,0,0)
assert(#engine.overlap(3,0,0,.4,{sensors=false}).entities==0);assert(engine.overlap(3,0,0,.4).entities[1]=='sensor')
end,update=function()
engine.set_velocity('player',4,0,0);for _,contact in engine.contacts('sensor') do if contact.phase=='enter' then engine.state_set('entered',true) end end
if engine.input('fire') then engine.destroy(engine.state_get('rig')) end
end})";
    lm::World w(p);const std::string rig=w.snapshot()["state"]["rig"];
    require(std::abs(entity(w,rig+"_tip")["position"][0].get<float>()-5)<.001f,"initial prefab parent transform failed");
    require(entity(w,rig+"_tip")["color"]==lm::Json({1,0,0,1}),"per-child prefab override failed");
    for(int i=0;i<60;++i)w.step(1.f/60,{});
    require(w.snapshot()["state"].value("entered",false),dimension+" sensor did not emit contact enter");
    require(entity(w,"player")["position"][0].get<float>()>3.8f,dimension+" sensor physically blocked player");
    require(std::abs(entity(w,rig+"_tip")["position"][0].get<float>()-6)<.04f,dimension+" child did not follow moving kinematic parent");
    const auto saved=w.save();w.restore(saved);require(std::abs(entity(w,rig+"_tip")["position"][0].get<float>()-6)<.04f,"save/load applied parent transform twice");
    lm::Input input;input.fire=true;w.step(1.f/60,input);require(w.snapshot()["entities"].size()==3,"destroying root failed to remove prefab subtree");
    require(w.statistics()["physics_bodies"]==2,"prefab destruction leaked kinematic body");
    auto cycle=p;cycle.prefabs["rig"]["entities"][1]["parent"]="tip";rejects([&]{lm::World bad(cycle);},"cycle");
}
void navigation(){auto p=sample();p.entities.erase(p.entities.begin()+1);p.document["navigation"]={{"cell_size",.25},{"agent_radius",.2},{"agent_height",1.0}};
    lm::Entity obstacle;obstacle.id="obstacle";obstacle.position={0,1,0};obstacle.size={2,2,8};p.entities.push_back(obstacle);
    lm::Navigation nav("3d",p.entities,p.document["navigation"]);const auto path=nav.path({-5,0,0},{5,0,0});
    require(path["status"]=="ready"&&path["points"].size()>=3,"Recast path did not route around obstacle");
    p.prefabs["follower"]={{"body","none"},{"position",{-7,.1,0}},{"size",{.3,.3,.3}},{"role","follower"}};
    p.script_source=R"(return {start=function()
for i=1,128 do local row=math.floor((i-1)/16);local col=(i-1)%16;local id=engine.spawn('follower',{position={-12+row*.6,.1,-5+col*.6}});engine.navigate(id,6+row*.6,.1,-5+col*.6,3) end
end})";
    lm::World w(p);for(int i=0;i<1000;++i)w.step(1.f/60,{});
    int crossed=0;const auto final=w.snapshot();for(const auto& e:final["entities"])if(e["role"]=="follower"&&e["position"][0].get<float>()>4)++crossed;
    require(crossed>=120,"128-agent crowd did not route around obstacle: crossed="+std::to_string(crossed));
    require(w.statistics()["navigation_agents"]==128,"crowd statistics lost agents");
    lm::Project flat=sample("2d");flat.entities.clear();flat.document["navigation"]={{"bounds",{-10,-10,10,10}},{"agent_radius",.2}};
    lm::Entity box;box.id="wall";box.position={0,0,0};box.size={2,8,1};flat.entities.push_back(box);
    lm::Navigation two("2d",flat.entities,flat.document["navigation"]);require(two.path({-5,0,0},{5,0,0})["status"]=="ready","top-down 2D navigation failed");
}
}
int main(){try{std::cout<<"viewport observation"<<std::endl;viewport_observation();std::cout<<"presentation save compatibility"<<std::endl;save_presentation_compatibility();std::cout<<"modules/state"<<std::endl;modules_and_state();std::cout<<"2D entities/queries"<<std::endl;lifetime_and_queries("2d");std::cout<<"3D entities/queries"<<std::endl;lifetime_and_queries("3d");
    std::cout<<"2D characters"<<std::endl;characters("2d");std::cout<<"3D characters"<<std::endl;characters("3d");std::cout<<"saves/scenes/camera"<<std::endl;save_scene_camera();std::cout<<"lock camera/contacts"<<std::endl;camera_and_contacts();
    std::cout<<"2D/3D moving support"<<std::endl;moving_support("2d");moving_support("3d");std::cout<<"joint attachments"<<std::endl;joint_attachments();
    std::cout<<"2D/3D prefab hierarchy/sensors"<<std::endl;hierarchy_and_sensors("2d");hierarchy_and_sensors("3d");std::cout<<"navigation/crowd"<<std::endl;navigation();
    std::cout<<"Game systems tests passed: "<<checks<<" checks; modules, state, 2D/3D lifetime/queries/controllers, save/scene/camera, 128-agent navigation\n";return 0;
}catch(const std::exception& error){std::cerr<<"Game systems failed: "<<error.what()<<'\n';return 1;}}
