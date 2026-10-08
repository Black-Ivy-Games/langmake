#include "langmake/material_grid.hpp"
#include "langmake/project.hpp"
#include "langmake/world.hpp"
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks=0;

void require(bool okay,const std::string& message) {
    ++checks;
    if(!okay) throw std::runtime_error(message);
}

void rejects(const std::function<void()>& run,const std::string& part) {
    try {
        run();
    } catch(const std::exception& error) {
        require(std::string(error.what()).find(part)!=std::string::npos,
            std::string("wrong error: ")+error.what());
        return;
    }
    throw std::runtime_error("Expected rejection: "+part);
}

lm::Project sample() {
    lm::Project p;
    p.document=lm::Json::object();
    p.dimension="2d";
    p.title="Material fixture";
    p.revision="material-grid-tests-v1";
    p.script_file="material_fixture.luau";
    p.script_source="return {}";
    p.entities={};
    p.gravity={0,0,0};
    return p;
}

lm::Project surface_sample() {
    auto p=sample();
    lm::AssetSource sprite;
    sprite.type="sprite";
    sprite.path="assets/material.sprite.json";
    sprite.document=lm::Json::parse(R"({
        "schema_version":1,"width":1,"height":1,
        "palette":{"R":"#FF0000","G":"#00FF00"},
        "frames":[{"id":"red","pixels":["R"]},{"id":"green","pixels":["G"]}],
        "animations":{
            "red":{"frames":["red"],"durations":[1],"loop":true},
            "green":{"frames":["green"],"durations":[1],"loop":true}
        },
        "default_animation":"red"
    })");
    p.assets["matter"]=sprite;
    lm::Entity left;
    left.id="left";
    left.body="none";
    left.asset="matter";
    left.animation="red";
    lm::Entity right=left;
    right.id="right";
    p.entities={left,right};
    return p;
}

std::uint32_t rgba(unsigned char r,unsigned char g,unsigned char b,unsigned char a=255) {
    return std::uint32_t(r)|(std::uint32_t(g)<<8)|(std::uint32_t(b)<<16)|(std::uint32_t(a)<<24);
}


void stronger_pull_holds_and_releases(){
    lm::MaterialGrid normal(32,24),strong(32,24);
    for(auto* g:{&normal,&strong}){
        for(int y=0;y<24;++y)g->put(y*32+16,26);
        for(int y=14;y<18;++y)for(int x=5;x<10;++x)g->put(y*32+x,10);
        g->put(17*32+20,10); // Fluid behind an unbroken wall cannot be pulled.
    }
    std::vector<int> held;normal.pull(7,5,20,{},held);
    for(int i=0;i<3;++i)strong.pull(7,5,20,{},held,true);
    auto height=[](const lm::MaterialGrid& g){double y=0;for(int i=0;i<g.n;++i)if(g.fields[0][i]==10&&i%32<16)y+=i/32;return y;};
    require(height(strong)<height(normal),"Stronger Pull must lift fluid faster");
    require(strong.fields[0][17*32+20]==10,"Pull moved fluid through solid wall");
    const double suspended=height(strong);std::vector<int> discoveries;
    for(int tick=1;tick<=30;++tick){strong.pull(7,5,20,{},held,true);const auto before=height(strong);strong.step(tick,{},held,{}, {},{},discoveries);require(height(strong)==before,"Held fluid fell during gravity step");}
    require(height(strong)<suspended,"Held Pull must continue gathering fluid");
    const double raised=height(strong);
    for(int tick=31;tick<=45;++tick)strong.step(tick,{}, {},{}, {},{},discoveries);
    require(height(strong)>raised,"Released fluid must resume falling");
    for(int y=0;y<24;++y)require(strong.fields[0][y*32+16]==26,"Pull altered solid wall");
}

void material_grid_storage() {
    rejects([]{ lm::MaterialGrid bad(0,1); }, "Material grid bounds");
    rejects([]{ lm::MaterialGrid bad(1,0); }, "Material grid bounds");
    rejects([]{ lm::MaterialGrid bad(1025,1); }, "Material grid bounds");
    rejects([]{ lm::MaterialGrid bad(513,513); }, "Material grid bounds");
    rejects([]{ lm::MaterialGrid bad(4,4,0); }, "Material grid bounds");
    rejects([]{ lm::MaterialGrid bad(4,4,9); }, "Material grid bounds");

    lm::MaterialGrid source(2,2);
    source.put(0,10,21,1,0);
    source.put(1,16,450,0,7);
    source.put(2,3,19,8,0);
    source.put(3,0,20,0,0);
    const auto packed=source.pack();

    lm::MaterialGrid exact(2,2);
    exact.unpack(packed,2,2);
    require(exact.pack()==packed,"exact material pack/unpack changed bytes");

    lm::MaterialGrid expanded(4,4);
    expanded.unpack(packed,2,2);
    for(int y=0;y<4;++y) for(int x=0;x<4;++x) {
        const int dst=expanded.index(x,y);
        const int src=source.index(x/2,y/2);
        require(expanded.fields[0][dst]==source.fields[0][src],"2x unpack expansion changed material");
        require(expanded.fields[1][dst]==source.fields[1][src],"2x unpack expansion changed temperature");
        require(expanded.fields[2][dst]==source.fields[2][src],"2x unpack expansion changed wetness");
        require(expanded.fields[3][dst]==source.fields[3][src],"2x unpack expansion changed burn");
    }
}

void material_grid_quiet_and_wake() {
    lm::MaterialGrid inert(16,16);
    inert.put(inert.index(0,0),2);
    std::vector<int> discoveries;
    inert.step(1,{}, {},lm::Json(),lm::Json(),{},discoveries);
    require(inert.sleeping,"rock/empty grid did not report sleeping");

    lm::MaterialGrid settled(32,16);
    for(int i=0;i<settled.n;++i) settled.put(i,10);
    for(int tick=1;tick<=13;++tick) settled.step(tick,{}, {},lm::Json(),lm::Json(),{},discoveries);
    require(settled.active[0]==0&&settled.active[1]==0,"settled material chunks did not go inactive after a 12-tick quiet cycle");
    settled.touch(settled.index(15,8));
    require(settled.active[0]&&settled.active[1],"boundary touch did not wake neighboring chunks");
    require(settled.dirty_revision[0]==settled.revision,"touched chunk dirty revision was not stamped");
}

void lua_material_api_validation() {
    auto p=sample();
    p.script_source=R"(return {start=function()
local q=engine.material_new(2,2,1)
q.m[1]=10
q.t[1]=22
local before=q.m[1]
local ok=pcall(function() engine.material_unpack(q,'bad',2,2) end)
assert(not ok)
assert(q.m[1]==before)
local bad={}
bad[#bad+1]=pcall(function() q.m[0]=1 end)
bad[#bad+1]=pcall(function() q.m[5]=1 end)
bad[#bad+1]=pcall(function() q.m[1]=27 end)
bad[#bad+1]=pcall(function() q.m[1]=1.5 end)
bad[#bad+1]=pcall(function() q.t[1]=0/0 end)
for _,ok in ipairs(bad) do assert(not ok) end
engine.state_set('survived',true)
end})";
    lm::World w(p);
    require(w.snapshot()["state"]["survived"],"material API validation script did not finish");

    p.script_source=R"(return {start=function()
local grids={}
for i=1,4 do grids[i]=engine.material_new(512,512,1) end
local ok,err=pcall(function() engine.material_new(1,1,1) end)
assert(not ok and string.find(err,'1048576'))
engine.state_set('limit_rejected',true)
end})";
    lm::World limited(p);
    require(limited.snapshot()["state"]["limit_rejected"],"native material live limit was not rejected");
}

void material_surface_refreshes() {
    auto p=surface_sample();
    p.script_source=R"(local q
local palette={}
for i=1,26 do palette[i]={asset='matter',clip='red'} end
palette[10]={asset='matter',clip='red'}
palette[11]={asset='matter',clip='green'}
return {
start=function()
 q=engine.material_new(1,1,1)
 q.m[1]=10
 engine.material_surface(q,'left',palette)
 engine.material_surface(q,'right',palette)
end,
update=function()
 if engine.input('fire') then
  q.m[1]=11
  engine.material_surface(q,'left',palette)
  engine.material_surface(q,'right',palette)
 elseif engine.input('jump') then
  q=engine.material_new(1,1,1)
  q.m[1]=10
  q.t[1]=21
  engine.material_surface(q,'left',palette)
 elseif engine.input('swim') then
  local packed=engine.material_pack(q)
  q.m[1]=11
  engine.material_unpack(q,packed,1,1)
  engine.state_set('unpack_revision',engine.material_revision(q))
  engine.material_surface(q,'left',palette)
 end
end})";
    lm::World world(p);
    const auto initial_left=world.surfaces().at("left").revision;
    const auto initial_right=world.surfaces().at("right").revision;
    require(world.surfaces().at("left").pixels==std::vector<std::uint32_t>(4,rgba(255,0,0)),
        "initial left material surface was not red");
    require(world.surfaces().at("right").pixels==std::vector<std::uint32_t>(4,rgba(255,0,0)),
        "initial right material surface was not red");

    lm::Input mutate;
    mutate.fire=true;
    world.step(1.f/60,mutate);
    require(world.surfaces().at("left").pixels==std::vector<std::uint32_t>(4,rgba(0,255,0))&&
            world.surfaces().at("right").pixels==std::vector<std::uint32_t>(4,rgba(0,255,0)),
        "same grid attached to two entities did not refresh both surfaces");
    require(world.surfaces().at("left").revision>initial_left&&world.surfaces().at("right").revision>initial_right,
        "surface revisions did not advance after shared grid mutation");
    const auto shared_left=world.surfaces().at("left").revision;

    lm::Input replace;
    replace.jump=true;
    world.step(1.f/60,replace);
    require(world.surfaces().at("left").pixels==std::vector<std::uint32_t>(4,rgba(255,0,0)),
        "new grid with same native revision did not draw expected material");
    require(world.surfaces().at("left").revision>shared_left,
        "new grid source with matching grid revision did not advance surface revision");
    const auto replaced_left=world.surfaces().at("left").revision;

    lm::Input unpack;
    unpack.actions["swim"]=1;
    world.step(1.f/60,unpack);
    require(world.snapshot()["state"]["unpack_revision"]==5,
        "material_unpack did not preserve previous grid revision plus one");
    require(world.surfaces().at("left").pixels==std::vector<std::uint32_t>(4,rgba(255,0,0)),
        "unpack into existing grid left stale surface pixels");
    require(world.surfaces().at("left").revision>replaced_left,
        "unpack into existing grid did not advance surface revision");
}

void state_bind_freshness_and_restore() {
    auto p=sample();
    p.document["project_id"]="state-bind-material";
    p.document["save_version"]=1;
    p.script_source=R"(local tick=0
return {
start=function()
 engine.state_set('regular',7)
 engine.state_bind('bound',function() return tick end)
 engine.state_bind('next',function() return tick+1 end)
end,
update=function()
 tick=tick+1
 engine.state_set('updates',engine.state_get('updates',0)+1)
 assert(engine.state_get('regular')==7)
 assert(engine.state_get('bound')==tick)
end,
on_load=function()
 tick=engine.state_get('bound')
 engine.state_bind('bound',function() return tick end)
 engine.state_bind('next',function() return tick+1 end)
end})";
    lm::World w(p);
    for(int i=0;i<20;++i) w.step(1.f/60,{});
    auto snap=w.snapshot();
    require(snap["state"]["updates"]==20,"state_bind fixture did not run 20 updates");
    require(snap["state"]["bound"]==20,"snapshot did not flush fresh bound state");
    require(snap["state"]["next"]==21,"snapshot did not evaluate all providers freshly");
    const auto saved=w.save();
    require(saved["state"]["bound"]==20&&saved["state"]["next"]==21,"save did not persist exact fresh bound state");

    lm::World loaded(p);
    loaded.restore(saved);
    loaded.step(1.f/60,{});
    require(loaded.snapshot()["state"]["bound"]==21&&loaded.snapshot()["state"]["next"]==22,
        "on_load rebind did not preserve restored provider state through next update");

    p.script_source=R"(local ticks=0
local calls=0
return {
start=function()
 engine.state_bind('deferred',function() calls=calls+1; return {ticks=ticks,calls=calls} end)
end,
update=function()
 ticks=ticks+1
 assert(calls==0)
end})";
    lm::World deferred(p);
    for(int i=0;i<20;++i) deferred.step(1.f/60,{});
    const auto deferred_snapshot=deferred.snapshot();
    require(deferred_snapshot["state"]["deferred"]==lm::Json({{"ticks",20},{"calls",1}}),
        "snapshot did not evaluate deferred provider exactly once");
    const auto deferred_save=deferred.save();
    require(deferred_save["state"]["deferred"]==lm::Json({{"ticks",20},{"calls",2}}),
        "save did not perform a fresh deferred provider evaluation");
}

void large_bound_state_roundtrip() {
    auto p=sample();
    p.document["project_id"]="large-bound-state";
    p.document["save_version"]=1;
    p.script_source=R"(local restored=false
local function payload()
 return string.rep('x',1100*1024)
end
return {
start=function()
 engine.state_bind('large',function() return payload() end)
end,
on_load=function()
 assert(#engine.state_get('large')==1100*1024)
 restored=true
 engine.state_bind('large',function() return payload() end)
end,
update=function()
 assert(restored)
end})";
    lm::World world(p);
    const auto saved=world.save();
    require(saved["state"]["large"].get_ref<const std::string&>().size()==1100*1024,
        "bound provider >1 MiB payload was not saved exactly");
    lm::World loaded(p);
    loaded.restore(saved);
    loaded.step(1.f/60,{});
    const auto roundtrip=loaded.save();
    require(roundtrip["state"]["large"].get_ref<const std::string&>().size()==1100*1024,
        "bound provider >1 MiB payload did not roundtrip after on_load rebind");
}

void state_bind_error_paths() {
    auto p=sample();
    p.script_source=R"(return {start=function()
engine.state_bind('value',function() return 1 end)
local ok=pcall(function() engine.state_bind('',function() return 2 end) end)
assert(not ok)
assert(engine.state_get('value')==1)
local bad_rebind=pcall(function() engine.state_bind('value',42) end)
assert(not bad_rebind)
assert(engine.state_get('value')==1)
engine.state_set('ordinary','old')
engine.state_bind('ordinary',function() return 'bound' end)
local replaced=pcall(function() engine.state_set('ordinary','new') end)
assert(not replaced)
assert(engine.state_get('ordinary')=='bound')
engine.state_bind('ordinary')
engine.state_set('ordinary','new')
engine.state_set('invalid_binding_preserved',engine.state_get('value'))
end})";
    lm::World w(p);
    const auto state=w.snapshot()["state"];
    require(state["ordinary"]=="new","ordinary state_set could overwrite bound key or unbind failed");
    require(state["invalid_binding_preserved"]==1,"invalid state_bind did not preserve prior binding");

    p.script_source=R"(return {start=function()
engine.state_bind('recursive',function() return engine.state_get('recursive') end)
end})";
    lm::World recursive(p);
    rejects([&]{ (void)recursive.snapshot(); }, "Recursive");

    p.script_source=R"(return {start=function()
engine.state_bind('forever',function() while true do end end)
end})";
    lm::World infinite(p);
    rejects([&]{ (void)infinite.snapshot(); }, "SCRIPT_BUDGET_EXCEEDED");
}

void state_bind_transactional_flush() {
    auto p=sample();
    p.script_source=R"(return {start=function()
engine.state_set('first','old')
engine.state_set('second','old')
engine.state_bind('first',function() return 'candidate' end)
engine.state_bind('second',function() error('second provider failed') end)
end,
update=function()
 if engine.input('fire') then
  engine.state_bind('first')
  engine.state_bind('second')
  assert(engine.state_get('first')=='old')
  engine.state_set('verified',true)
 end
end})";
    lm::World w(p);
    rejects([&]{ (void)w.snapshot(); }, "second provider failed");
    lm::Input input;
    input.fire=true;
    w.step(1.f/60,input);
    require(w.snapshot()["state"]["verified"],"failed provider committed an earlier candidate state");

    p.script_source=R"(return {start=function()
engine.state_set('first','old')
engine.state_set('second','old')
engine.state_bind('first',function() return 'candidate' end)
engine.state_bind('second',function() error('second provider failed') end)
local ok=pcall(function() engine.state_get('second') end)
assert(not ok)
engine.state_bind('first')
engine.state_bind('second')
assert(engine.state_get('first')=='old')
engine.state_set('verified',true)
end})";
    lm::World checked(p);
    require(checked.snapshot()["state"]["verified"],"failed provider committed an earlier candidate state");
}
}

int main() {
    try {
        std::cout<<"material grid storage"<<std::endl;
        material_grid_storage();
        stronger_pull_holds_and_releases();
        std::cout<<"material grid quiet/wake"<<std::endl;
        material_grid_quiet_and_wake();
        std::cout<<"Lua material API validation"<<std::endl;
        lua_material_api_validation();
        std::cout<<"material surface refreshes"<<std::endl;
        material_surface_refreshes();
        std::cout<<"state_bind freshness/restore"<<std::endl;
        state_bind_freshness_and_restore();
        std::cout<<"large bound state roundtrip"<<std::endl;
        large_bound_state_roundtrip();
        std::cout<<"state_bind error paths"<<std::endl;
        state_bind_error_paths();
        std::cout<<"state_bind transactional flush"<<std::endl;
        state_bind_transactional_flush();
        std::cout<<"Material grid tests passed: "<<checks<<" checks\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"Material grid tests failed: "<<error.what()<<'\n';
        return 1;
    }
}
