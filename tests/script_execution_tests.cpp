#include "langmake/world.hpp"
#include "../src/script_budget.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using lm::detail::ScriptBudget;
void require(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
lm::Project project(std::string script){
    lm::Project result;result.dimension="2d";result.title="Isolated script execution fixture";
    result.revision="execution-fixture";result.script_file="scripts/fixture.luau";
    result.script_source=std::move(script);result.gravity={0,0,0};return result;
}
template<class Action> lm::Json failure(Action action,const char* code){
    try{action();}catch(const std::runtime_error& error){auto result=lm::Json::parse(error.what(),nullptr,false);
        if(result.is_discarded())throw std::runtime_error(std::string("Expected structured ")+code+", received "+error.what());
        if(result.at("code")!=code)throw std::runtime_error(std::string("Expected ")+code+", received "+result.dump());
        for(const char* key:{"code","phase","file","tick","message","source_revision"})require(result.contains(key),"Legacy diagnostic field missing");
        return result;}
    throw std::runtime_error("Expected script rejection");
}
struct Clock {
    std::int64_t wall=0,cpu=0;bool available=true;
    static ScriptBudget::Reading read(void* context){auto& clock=*static_cast<Clock*>(context);return {clock.wall,{clock.cpu,clock.available,17}};}
};
void deterministic_clock(){
    Clock clock;ScriptBudget budget(ScriptBudget::Policy::require_cpu,Clock::read,&clock);budget.begin();
    clock.wall=90000000;clock.cpu=1000000;
    require(!budget.poll(true),"Elapsed delay was charged as CPU");
    require(budget.sample().elapsed_ns==90000000&&budget.sample().cpu_ns==1000000,"Separate measurements missing");
    clock.wall+=1000000;clock.cpu=51000000;require(budget.poll(true),"Final native CPU work escaped the guard");
    clock.cpu=1000000;require(budget.poll(true),"Exceeded budget lost its sticky verdict");
    budget.begin();require(!budget.poll(true),"Fresh call retained previous exhaustion");
    clock.available=false;require(budget.poll(true)&&budget.sample().clock_failed&&!budget.sample().cpu_available,"CPU read failure removed protection");
    budget.begin();require(budget.sample().clock_failed,"Initialization failure was ignored");
    ScriptBudget mobile(ScriptBudget::Policy::prefer_cpu,Clock::read,&clock);mobile.begin();
    clock.wall+=60000000;require(mobile.poll(true)&&mobile.sample().fallback&&mobile.sample().exceeded,"Explicit mobile elapsed fallback changed");
    require(!mobile.sample().cpu_available,"Unavailable CPU was reported as measured zero");
    clock.available=true;clock.cpu=100000000;budget.begin();clock.cpu=99999999;
    require(budget.poll(true)&&budget.sample().clock_failed,"Regressed CPU clock was accepted");
    clock.cpu=100000000;budget.begin();bool rejected=false;
    std::thread other([&]{rejected=budget.poll(true)&&budget.sample().clock_failed;});other.join();
    require(rejected,"Budget moved to another executing thread");
}
void actual_clock(){
#ifdef _WIN32
    ScriptBudget delay;delay.begin();std::this_thread::sleep_for(std::chrono::milliseconds(90));
    require(!delay.poll(true),"Sleeping native thread falsely exceeded its CPU allowance");
    require(delay.sample().elapsed_ns>=80000000&&delay.sample().cpu_available,"Windows thread clock unavailable");
    ScriptBudget busy;busy.begin();std::uint64_t iterations=0;
    while(!busy.poll()){++iterations;std::atomic_signal_fence(std::memory_order_seq_cst);}
    require(busy.sample().exceeded&&!busy.sample().clock_failed&&busy.sample().cpu_ns>=ScriptBudget::allowance_ns,"Real CPU loop escaped accounting");
    require(busy.sample().elapsed_ns<5000000000LL,"CPU loop did not terminate within fixture timeout");
    std::cout<<lm::Json{{"case","actual_windows_clock"},{"sleep_cpu_ms",double(delay.sample().cpu_ns)/1e6},
        {"sleep_elapsed_ms",double(delay.sample().elapsed_ns)/1e6},{"busy_cpu_ms",double(busy.sample().cpu_ns)/1e6},
        {"busy_elapsed_ms",double(busy.sample().elapsed_ns)/1e6},{"cpu_queries",busy.sample().queries},{"iterations",iterations}}.dump()<<'\n';
#endif
}
void loops_and_locations(){
    for(const std::string prefix:{"", "--!native\n"}){
        auto load=project(prefix+"while true do end");auto load_error=failure([&]{lm::World world(load);},"SCRIPT_BUDGET_EXCEEDED");
        require(load_error.at("phase")=="load","Load phase missing");
        auto start=project(prefix+"return {start=function() while true do end end}");
        require(failure([&]{lm::World world(start);},"SCRIPT_BUDGET_EXCEEDED").at("phase")=="start","Start loop escaped");
        for(const std::string loop:{"while true do end", "pcall(function() while true do end end)",
            "while true do pcall(function() while true do end end) end", "xpcall(function() while true do end end,function() while true do end end)"}){
            lm::World world(project(prefix+"return {update=function() "+loop+" end}"));
            auto error=failure([&]{world.step(1.f/60,{});},"SCRIPT_BUDGET_EXCEEDED");
            require(error.at("phase")=="update"&&error.at("execution").at("elapsed_ms").is_number(),"Hook timing missing");
#ifdef _WIN32
            require(error.at("execution").at("cpu_ms").is_number(),"Windows hook CPU measurement missing");
#endif
            require(error.at("location").at("line").get<int>()>0&&!error.at("stack").empty(),"Interrupted location missing");
            require(error.at("vm_memory_bytes").get<std::size_t>()<16*1024*1024,"Fixture exhausted memory instead of CPU");
            require(world.script_diagnostic()==error,"Embedded host did not retain serialized diagnostic");
            require(failure([&]{world.step(1.f/60,{});},"SCRIPT_BUDGET_EXCEEDED")==error,"Failed world continued an interrupted update");
        }
    }
    auto module=project("local M=require('worker')\nreturn {update=function() M.run() end}");
    module.modules["worker"]="local M={}\nfunction M.run()\n while true do end\nend\nreturn M";
    module.module_files["worker"]="modules/worker.luau";lm::World world(module);
    auto error=failure([&]{world.step(1.f/60,{});},"SCRIPT_BUDGET_EXCEEDED");
    require(error.at("location").at("file")=="modules/worker.luau","Module location lost at unwind");
    for(const auto* body:{"local x=string.rep('x',32*1024*1024)","while true do end"}){
        auto nested=project("return {update=function() require('worker') end}");
        nested.modules["worker"]=std::string(body)+"\nreturn {}";nested.module_files["worker"]="modules/worker.luau";
        lm::World module_world(nested);const auto* code=body[0]=='l'?"SCRIPT_MEMORY_LIMIT":"SCRIPT_BUDGET_EXCEEDED";
        failure([&]{module_world.step(1.f/60,{});},code);
        lm::World bound(project(std::string("return {start=function() engine.state_bind('view',function() ")+body+
            " return 42 end) end,update=function() engine.state_get('view') end}"));
        failure([&]{bound.step(1.f/60,{});},code);
    }
}
void ordinary_memory_and_observation(){
    lm::World ordinary(project("return {update=function() error('ordinary failure') end}"));
    auto error=failure([&]{ordinary.step(1.f/60,{});},"SCRIPT_ERROR");
    require(error.at("message").get<std::string>().find("ordinary failure")!=std::string::npos&&error.contains("location"),"Ordinary error lost source information");
    lm::World memory(project("return {update=function() local x=string.rep('x',32*1024*1024) end}"));
    auto oom=failure([&]{memory.step(1.f/60,{});},"SCRIPT_MEMORY_LIMIT");
    require(oom.at("vm_memory_limit_bytes")==16*1024*1024&&oom.at("stack").size()<=16,"Memory failure capture is unbounded");
    lm::World caught_memory(project("return {update=function() pcall(function() string.rep('x',32*1024*1024) end);error('ordinary after caught memory') end}"));
    failure([&]{caught_memory.step(1.f/60,{});},"SCRIPT_ERROR");
    lm::World arbitrary_message(project("return {update=function() error(string.rep(string.char(255),3000)) end}"));
    auto clipped=failure([&]{arbitrary_message.step(1.f/60,{});},"SCRIPT_ERROR");
    require(clipped.at("message").get<std::string>().size()<7000&&arbitrary_message.script_diagnostic()==clipped,"Arbitrary error bytes broke bounded diagnostic serialization");
    lm::World invalid_hook(project("return {update=42}"));
    auto invalid=failure([&]{invalid_hook.step(1.f/60,{});},"SCRIPT_ERROR");
    require(invalid.at("execution").at("timing_available")==false&&invalid.at("execution").at("cpu_ms").is_null(),"Invalid hook reused a previous hook's timing");
    lm::World observed(project(R"(return {
      start=function() engine.state_set('committed',42);engine.state_bind('view',function() error('provider must stay idle after failure') end) end,
      update=function() error('original update failure') end
    })"));
    auto original=failure([&]{observed.step(1.f/60,{});},"SCRIPT_ERROR");
    require(observed.tick()==0&&observed.script_diagnostic()==original,"Passive failure access changed diagnostics");
    require(observed.snapshot().at("state").at("committed")==42,"Failed-world observation ran a provider");
    require(failure([&]{observed.save();},"SCRIPT_ERROR")==original,"Failed world was saved as a completed checkpoint");
    require(failure([&]{observed.change_scene(observed.current_project().scene_id);},"SCRIPT_ERROR")==original,
        "Scene change bypassed the failed-world reconstruction requirement");
    lm::World provider(project("return {start=function() engine.state_bind('view',function() while true do end end) end}"));
    require(failure([&]{provider.snapshot();},"SCRIPT_BUDGET_EXCEEDED").at("phase")=="snapshot","Snapshot provider bypassed the guard");
}
void repeated_hooks_and_recovery(){
    auto source=project(R"(return {
      start=function() engine.state_set('count',0) end,
      update=function() if engine.input('fire') then engine.state_set('partial',true);error('interrupted') end;engine.state_set('count',engine.state_get('count')+1) end,
      on_load=function() engine.state_set('loaded',true) end,
      on_recover=function() engine.state_set('recovered',true) end
    })");
    lm::World first(source),second(source);first.set_script_profiling(true);
    for(int i=0;i<300;++i){first.step(1.f/60,{});second.step(1.f/60,{});}
    require(first.tick()==300&&second.tick()==300,"Fresh budgets leaked between hooks or worlds");
    const auto execution=first.script_execution();require(execution.at("bindings").at("state_set").at("calls")==1,"Binding measurements are not per hook");
    auto saved=first.save();lm::Input input;input.fire=true;
    failure([&]{first.step(1.f/60,input);},"SCRIPT_ERROR");first.restore(saved,true);
    require(first.script_execution().at("phase")=="on_recover"&&first.script_execution().at("bindings").at("state_set").at("calls")==1,
        "Host binding profiling was lost inside the recovery hook");
    second.set_script_profiling(true);second.restore(saved);
    require(second.script_execution().at("phase")=="on_load"&&second.script_execution().at("bindings").at("state_set").at("calls")==1,
        "Host binding profiling was lost inside the load hook");
    require(first.snapshot().at("state").at("recovered")==true&&!first.snapshot().at("state").contains("partial"),"Recovery resumed partial failed state");
    require(first.script_diagnostic().is_null(),"Restored world retained stale failure");
    first.step(1.f/60,{});require(first.snapshot().at("state").at("count")==301,"Recovered world cannot update");
}
void native_final_operation(){
    auto source=project("return {update=function() engine.navigation_rebuild() end}");
    source.document["navigation"]={{"enabled",true},{"bounds",{-128,-128,128,128}},{"cell_size",.25}};
    lm::World world(source);
    auto error=failure([&]{world.step(1.f/60,{});},"SCRIPT_BUDGET_EXCEEDED");
    const auto& execution=error.at("execution");
    require((execution.at("cpu_ms").is_number()?execution.at("cpu_ms"):execution.at("elapsed_ms")).get<double>()>=50,
        "Final native work was not charged to the hook");
    require(error.at("stack").at(0).at("function")=="navigation_rebuild","Final native binding attribution missing");
    std::cout<<lm::Json{{"case","bounded_native_final_operation"},{"execution",error.at("execution")},{"location",error.at("location")}}.dump()<<'\n';
}
void migration_metadata(){
    auto old=project("return {start=function() engine.state_set('coins',3) end}");
    old.document["project_id"]="script-execution-migration";old.document["save_version"]=1;
    lm::World original(old);for(int i=0;i<3;++i)original.step(1.f/60,{});const auto saved=original.save();
    auto revised=old;revised.revision="execution-migrated";revised.document["save_version"]=2;
    revised.script_source=R"(return {migrate=function(saved,from,to)
      assert(from==1 and to==2);saved.tick=0;saved.time=0;saved.spawn_serial=999;saved.random_state=0
      saved.project_id='wrong';saved.source_revision='wrong';saved.game_save_version=999
      saved.state.coins=6;return saved end})";
    lm::World migrated(revised);migrated.restore(saved);const auto after=migrated.save();
    for(const auto* key:{"tick","time","spawn_serial","random_state"})require(after.at(key)==saved.at(key),"Migration changed immutable engine metadata");
    require(after.at("project_id")=="script-execution-migration"&&after.at("source_revision")=="execution-migrated"&&
        after.at("game_save_version")==2&&after.at("state").at("coins")==6,"Migration identity/version or authored state was not preserved");
    auto oversized_lua_input=saved;auto& rows=oversized_lua_input["state"]["rows"];
    rows=lm::Json::array({lm::Json::array(),lm::Json::array(),lm::Json::array()});
    for(int i=0;i<150000;++i)rows[i/50000].push_back(lm::Json{{"n",i},{"a",i},{"b",i},{"c",i}});
    auto decode_error=failure([&]{migrated.restore(oversized_lua_input);},"SCRIPT_MEMORY_LIMIT");
    require(decode_error.at("execution").at("timing_available")==false&&decode_error.at("execution").at("cpu_ms").is_null(),
        "Failed native migration decoding was charged as authored hook CPU");
    require(migrated.save()==after,"Failed migration decoding modified the running world");
}
}
int main(){try{
    deterministic_clock();actual_clock();loops_and_locations();ordinary_memory_and_observation();repeated_hooks_and_recovery();native_final_operation();migration_metadata();
    std::cout<<"PASS script CPU guard, fallback/failure clocks, interpreted/native loops, diagnostics, passive observation, binding profiling and transactional recovery\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
