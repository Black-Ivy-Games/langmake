#include "langmake/timeline.hpp"
#include <cmath>
namespace lm {
Input parse_input(const Json& j) {
    if(!j.is_object())throw std::runtime_error("Input must be an object");
    Input i;i.move_x=j.value("move_x",0.0f);i.move_z=j.value("move_z",0.0f);i.jump=j.value("jump",false);i.fire=j.value("fire",false);
    auto axis=[](float n){if(!std::isfinite(n)||std::abs(n)>1)throw std::runtime_error("Input axes must be finite and within [-1,1]");};
    axis(i.move_x);axis(i.move_z);
    if(j.contains("actions")) {
        if(!j["actions"].is_object()||j["actions"].size()>64)throw std::runtime_error("actions must be an object with at most 64 actions");
        for(auto&[key,v]:j["actions"].items()){if(key.empty()||key.size()>64)throw std::runtime_error("Invalid input action name");float n=v.is_boolean()?(v.get<bool>()?1.f:0.f):v.get<float>();axis(n);i.actions[key]=n;}
    }
    if(j.contains("pointer")){if(!j["pointer"].is_array()||j["pointer"].size()!=2)throw std::runtime_error("pointer must have two normalized coordinates");i.pointer=j["pointer"].get<std::array<float,2>>();for(float n:i.pointer)if(!std::isfinite(n)||n<0||n>1)throw std::runtime_error("pointer coordinates must be 0..1");}
    if(j.contains("events")){if(!j["events"].is_array()||j["events"].size()>64||j["events"].dump().size()>8192)throw std::runtime_error("Input events exceed bounds");i.events=j["events"];}
    return i;
}
Json input_json(const Input&i){return {{"move_x",i.move_x},{"move_z",i.move_z},{"jump",i.jump},{"fire",i.fire},{"actions",i.actions},{"pointer",i.pointer},{"events",i.events}};}
Timeline::Timeline(const Json& document) {
    if(!document.is_object())throw std::runtime_error("Timeline must be an object");
    auto list=document.value("events",Json::array());if(!list.is_array()||list.size()>10000)throw std::runtime_error("Timeline requires at most 10000 events");
    for(auto&e:list){for(auto key:{"from","to"})if(!e.contains(key)||!e[key].is_number_integer()||e[key]<0||e[key]>36000)throw std::runtime_error("Input event range must be integer timeline ticks within 0..36000");int from=e["from"],to=e["to"];if(to<from)throw std::runtime_error("Input event 'to' precedes 'from'");events.push_back({from,to,parse_input(e)});}
}
Input Timeline::at(int tick) const {Input input;for(const auto&e:events)if(tick>=e.from&&tick<e.to)input=e.input;return input;}
}
