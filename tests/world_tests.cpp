#include "langmake/world.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

lm::Project sample(const std::string& dimension) {
    lm::Project project;
    project.dimension = dimension;
    project.title = "World test";
    project.revision = "test-revision";
    project.script_file = "scripts/test.luau";
    lm::Entity floor;
    floor.id = "floor";
    floor.position = {0, -0.5f, 0};
    floor.size = {100, 1, 100};
    floor.role = "ground";
    lm::Entity player;
    player.id = "player";
    player.role = "player";
    player.body = "dynamic";
    player.position = {0, 3, 0};
    lm::Entity coin;
    coin.id = "coin";
    coin.role = "collectible";
    coin.body = "none";
    coin.position = {2, 0.5f, 0};
    project.entities = {floor, player, coin};
    project.script_source = R"(
return {
    update = function(dt)
        engine.move_player(engine.input("move_x") * 5, engine.input("move_z") * 5)
        if engine.input("jump") then engine.jump(7) end
    end
}
)";
    return project;
}

lm::Json entity(const lm::World& world, const std::string& id) {
    const auto snapshot = world.snapshot();
    for (const auto& entity : snapshot["entities"]) {
        if (entity["id"] == id) return entity;
    }
    throw std::runtime_error("Missing snapshot entity " + id);
}

void rejects(const std::function<void()>& action, const std::string& expected) {
    try { action(); }
    catch (const std::runtime_error& error) {
        require(std::string(error.what()).find(expected) != std::string::npos,
            "Expected diagnostic " + expected + ", received " + error.what());
        return;
    }
    throw std::runtime_error("Expected rejection: " + expected);
}

void physics(const std::string& dimension) {
    auto project = sample(dimension);
    lm::World world(project);
    for (int frame = 0; frame < 120; ++frame) world.step(1.0f / 60, {});
    const float resting_y = entity(world, "player")["position"][1];
    require(resting_y > 0.35f && resting_y < 0.65f, dimension + " player must land on real physics floor");
    require(world.snapshot()["grounded"], dimension + " player must become grounded");
    for (int frame = 0; frame < 15; ++frame) world.step(1.0f / 60, {1, 1, false});
    auto position = entity(world, "player")["position"];
    require(position[0].get<float>() > 0.8f, dimension + " Luau must drive the physics player");
    if (dimension == "3d") require(position[2].get<float>() > 0.8f, "3d must move on z axis");
    else require(position[2].get<float>() == 0, "2d must preserve authored z");
    for (int frame = 0; frame < 15; ++frame) world.step(1.0f / 60, {0, 0, true});
    const float jump_y = entity(world, "player")["position"][1];
    require(jump_y > resting_y + 0.6f && jump_y < resting_y + 2.0f,
        dimension + " grounded jump must work without repeatedly jumping in air");
    for (int frame = 0; frame < 120; ++frame) world.step(1.0f / 60, {});
    require(world.snapshot()["grounded"], dimension + " player must land after jumping");
    require(world.snapshot()["tick"] == 270, "snapshot must track simulation tick");
    require(world.snapshot()["source_revision"] == "test-revision", "snapshot must identify source revision");
}

void script_api() {
    auto project = sample("2d");
    project.script_source = R"(
return {
    start = function()
        engine.set_position("player", 1, 0.5, 0)
        local x, y, z = engine.position("player")
        assert(x == 1 and y == 0.5 and z == 0)
        assert(engine.distance("player", "coin") == 1)
        assert(engine.collect("coin"))
        assert(not engine.collect("coin"))
        assert(engine.collected() == 1)
        engine.set_color("player", 0.2, 0.3, 0.4)
        engine.message("one collectible")
        engine.win()
    end
}
)";
    lm::World world(project);
    require(world.won() && world.collected() == 1 && world.message() == "one collectible", "game state script API");
    require(!entity(world, "coin")["visible"].get<bool>(), "collected entities must become hidden");
    require(entity(world, "coin")["collected"], "snapshot must expose collection state");
    require(std::abs(entity(world, "player")["color"][0].get<float>() - 0.2f) < 0.001f, "color change must reach ECS");
    world.step(1.0f / 60, {});
    require(std::abs(entity(world, "player")["position"][0].get<float>() - 1) < 0.01f, "teleport must update physics as well as ECS");
}

lm::Project asset_sample(const std::string& dimension = "2d") {
    auto project = sample(dimension);
    project.gravity = {0, 0, 0};
    project.script_source = "return {}";
    lm::AssetSource sprite;
    sprite.type = "sprite";
    sprite.path = "assets/test.sprite.json";
    sprite.document = lm::Json::parse(R"({
        "schema_version":1,"width":1,"height":1,
        "palette":{"R":"#FF0000","G":"#00FF00"},
        "frames":[{"id":"red","pixels":["R"]},{"id":"green","pixels":["G"]}],
        "animations":{
            "walk":{"frames":["red","green"],"durations":[0.06,0.06],"loop":true},
            "once":{"frames":["red","green"],"durations":[0.06,0.06],"loop":false}
        },"default_animation":"walk"
    })");
    project.assets["sprite"] = sprite;
    project.entities[1].asset = "sprite";
    project.entities[1].animation = "walk";
    project.entities[1].color = {1, 1, 1, 1};
    lm::AssetSource music;
    music.type = "music";
    music.path = "assets/beep.music.json";
    music.document = lm::Json::parse(R"({
        "schema_version":1,"type":"music","length":96,
        "instruments":{"beep":{"wave":"sine"}},
        "notes":[{"tick":0,"duration":24,"note":60,"instrument":"beep"}]
    })");
    project.assets["beep"] = music;
    return project;
}

void animation_api() {
    auto project = asset_sample();
    project.entities[2].asset = "sprite";
    project.entities[2].animation = "once";
    project.entities[2].animation_speed = 2;
    project.entities[2].flip_x = true;
    project.script_source = R"(
return {update = function(dt)
    engine.play_animation("player", "walk", engine.input("jump"))
end}
)";
    lm::World world(project);
    require(world.assets().sprites.size() == 1 && world.assets().music.size() == 1,
        "World must expose compiled assets to preview without recompiling recipes");
    require(entity(world, "player")["animation_frame"] == "red", "Initial animation frame is incorrect");
    world.step(.04f, {});
    require(entity(world, "player")["animation_frame"] == "red", "Animation advanced before its duration");
    require(entity(world, "coin")["animation_frame"] == "green" && entity(world, "coin")["flip_x"],
        "Authored animation speed and sprite flip must reach headless inspection");
    world.step(.04f, {});
    require(entity(world, "player")["animation_frame"] == "green",
        "Repeated play_animation on the same clip must preserve its timeline");
    world.step(.04f, {0, 0, true});
    require(entity(world, "player")["animation_frame"] == "red", "Explicit animation restart must reset the timeline");
    require(std::abs(world.time() - .12) < 1e-6 && std::abs(entity(world, "player")["animation_time"].get<double>() - .04) < 1e-6,
        "World time and restarted animation time must remain distinct");
    for (int i = 0; i < 10; ++i) world.step(.04f, {});
    require(entity(world, "coin")["animation_frame"] == "green", "Nonlooping animation must hold its final frame");
    const auto displayed = world.entities();
    require(std::abs(displayed[1].animation_time - entity(world, "player")["animation_time"].get<double>()) < 1e-6,
        "Renderer and inspection must use the same animation clock");
}

void visual_and_velocity_api(const std::string& dimension) {
    auto project = asset_sample(dimension);
    project.script_source = R"(
return {
    start = function()
        engine.set_rotation("player", 10, 20, 30)
        engine.set_visible("coin", false)
        engine.set_color("player", 0.3, 0.6, 0.9)
        engine.set_velocity("player", 2, 3, 4)
    end,
    update = function(dt)
        if engine.input("fire") then engine.set_visible("coin", true) end
    end
}
)";
    lm::World world(project);
    require(entity(world, "player")["rotation"] == lm::Json({10, 20, 30}), "Visual rotations must appear in state inspection");
    require(!entity(world, "coin")["visible"].get<bool>(), "set_visible must hide the entity");
    world.step(.05f, {0, 0, false, true});
    require(entity(world, "coin")["visible"], "Fire action must reach Luau and reveal the entity");
    const auto player = entity(world, "player");
    require(std::abs(player["position"][0].get<float>() - .1f) < .005f &&
        std::abs(player["position"][1].get<float>() - 3.15f) < .005f,
        dimension + " set_velocity must move the real physics body");
    require(std::abs(player["position"][2].get<float>() - (dimension == "3d" ? .2f : 0.f)) < .005f,
        dimension + " velocity z handling is incorrect");
    require(world.entities()[1].rotation == lm::Vec3{10, 20, 30} && std::abs(world.entities()[1].color[0] - .3f) < .001f,
        "Visual rotation and tint must reach the preview entity list");
    project.script_source = "return {start=function() engine.collect('coin'); engine.set_visible('coin',true) end}";
    lm::World collected(project);
    require(!entity(collected, "coin")["visible"].get<bool>(), "A collected entity cannot be shown again");
}

void audio_event_api() {
    auto project = asset_sample();
    project.script_source = R"(
return {
    start = function() engine.play_music("beep", false); engine.play_sound("beep") end,
    update = function(dt)
        if engine.input("fire") then engine.stop_music() else engine.play_music("beep") end
    end
}
)";
    lm::World world(project);
    const auto initial = world.audio_events();
    require(initial.size() == 2 && initial[0]["action"] == "music" && initial[1]["action"] == "sound",
        "Headless audio must preserve script event order");
    require(initial[0]["serial"] == 1 && initial[1]["serial"] == 2 && initial[0]["tick"] == 0 &&
        !initial[0]["loop"].get<bool>() && initial[1]["asset"] == "beep", "Audio event metadata is incorrect");
    require(world.snapshot()["music"] == "beep", "Music state must reflect the active score");
    world.step(.02f, {});
    require(world.audio_events().back()["loop"], "play_music must loop by default");
    world.step(.02f, {0, 0, false, true});
    require(world.snapshot()["music"] == "" && world.audio_events().back()["action"] == "stop_music" &&
        world.audio_events().back()["serial"] == 4 && world.audio_events().back()["tick"] == 1,
        "Stopping music must be visible to both the mixer and state inspection");
    require(world.snapshot()["audio_events"] == world.audio_events(), "Snapshot audio events must match mixer events");
    project.script_source = "return {update=function() for i=1,32 do engine.play_sound('beep') end end}";
    lm::World bounded(project);
    for (int i = 0; i < 5; ++i) bounded.step(.01f, {});
    const auto retained = bounded.audio_events();
    require(retained.size() == 128 && retained.front()["serial"] == 33 && retained.back()["serial"] == 160,
        "Audio history must stay bounded while preserving monotonic event serials");
}

void asset_api_errors() {
    auto project = asset_sample();
    for (const auto& [script, diagnostic] : std::vector<std::pair<std::string, std::string>>{
        {"engine.play_animation('player','missing')", "unknown sprite animation"},
        {"engine.play_animation('floor','walk')", "requires a sprite entity"},
        {"engine.play_sound('missing')", "unknown music asset"},
        {"engine.play_music('sprite')", "unknown music asset"},
        {"engine.set_visible('player',1)", "boolean"},
        {"engine.set_rotation('player',0/0,0,0)", "finite"},
        {"engine.set_velocity('floor',1,0,0)", "dynamic body"},
        {"for i=1,33 do engine.play_sound('beep') end", "audio event limit"}
    }) {
        project.script_source = "return {start=function() " + script + " end}";
        rejects([&] { lm::World world(project); }, diagnostic);
    }
    project.script_source = "return {}";
    project.entities[1].animation = "missing";
    rejects([&] { lm::World world(project); }, "animation");
}

void shaped_rotating_bodies() {
    auto p=sample("2d");p.entities[1].role="matter";p.entities[1].size={4,4,4};p.entities[1].rotation={0,0,23};
    p.entities[1].physics2d={{"rotate",true},{"angular_damping",.3},{"polygons",{{{-.45,-.12},{.4,-.12},{.3,.16},{-.4,.22}}}}};
    p.script_source="return {}";lm::World w(p);
    for(int i=0;i<180;++i)w.step(1.f/60,{});
    const auto resting=entity(w,"player");
    require(resting["position"][1].get<float>()<1.3f,"Polygon must rest on its outline, not the former four-unit circle");
    require(std::abs(resting["rotation"][2].get<float>()-23)>10,"Off-center contact must physically rotate a shaped body");
    require(!w.snapshot()["contacts"].empty(),"Shaped body resting contact missing");
    auto spinning=p;spinning.gravity={0,0,0};spinning.script_source=R"(return {start=function() engine.set_angular_velocity('player',90) end})";
    lm::World source(spinning);for(int i=0;i<12;++i)source.step(1.f/60,{});auto saved=source.save();
    lm::World restored(spinning);restored.restore(saved);
    require(std::abs(entity(source,"player")["angular_velocity"].get<float>()-entity(restored,"player")["angular_velocity"].get<float>())<.001f,"Save must preserve angular velocity");
    for(int i=0;i<20;++i){source.step(1.f/60,{});restored.step(1.f/60,{});}
    require(std::abs(entity(source,"player")["rotation"][2].get<float>()-entity(restored,"player")["rotation"][2].get<float>())<.01f,"Restored rotation must continue without a jump");
    auto invalid=p;invalid.entities[1].physics2d["polygons"][0]={{-.4,-.4},{.4,.4},{.4,-.4},{-.4,.4}};
    rejects([&]{lm::World rejected(invalid);},"convex");
    invalid=p;invalid.dimension="3d";rejects([&]{lm::World rejected(invalid);},"2D");
}

void sandbox_and_errors() {
    auto project = sample("2d");
    project.script_source = "return {start=function() assert(io==nil and loadfile==nil and os.execute==nil and print==nil) end}";
    { lm::World world(project); }
    project.script_source = "return setmetatable({}, {__index=function() error('hooks must not invoke metatables') end})";
    { lm::World world(project); world.step(1.0f / 60, {}); }
    project.script_source = "return {start=function() engine.win=nil end}";
    rejects([&] { lm::World world(project); }, "SCRIPT_ERROR");
    project.script_source = "return {start=function() engine.position('missing') end}";
    rejects([&] { lm::World world(project); }, "unknown entity");
    project.script_source = "return {start=function() engine.set_position('player', 0/0, 0, 0) end}";
    rejects([&] { lm::World world(project); }, "finite");
    project.script_source = "return {";
    rejects([&] { lm::World world(project); }, "scripts/test.luau");
    project.script_source = "return 7";
    rejects([&] { lm::World world(project); }, "must return a table");
    project.script_source = "while true do end";
    rejects([&] { lm::World world(project); }, "SCRIPT_BUDGET_EXCEEDED");
    project.script_source = "return {update=function() while true do end end}";
    { lm::World world(project); rejects([&] { world.step(1.0f / 60, {}); }, "SCRIPT_BUDGET_EXCEEDED"); }
    project.script_source = "return {update=function() pcall(function() while true do end end) end}";
    { lm::World world(project); rejects([&] { world.step(1.0f / 60, {}); }, "SCRIPT_BUDGET_EXCEEDED"); }
    project.script_source = "return {start=function() local large=string.rep('x', 32*1024*1024) end}";
    rejects([&] { lm::World world(project); }, "SCRIPT_MEMORY_LIMIT");
    project.script_source = "return {}";
    project.entities[1].shape = "sphere";
    project.entities[1].size = {1, 2, 1};
    rejects([&] { lm::World world(project); }, "sphere size must be equal");
}
}

int main() {
    try {
        physics("2d");
        physics("3d");
        script_api();
        animation_api();
        visual_and_velocity_api("2d");
        visual_and_velocity_api("3d");
        audio_event_api();
        asset_api_errors();
        shaped_rotating_bodies();
        sandbox_and_errors();
        std::cout << "World tests passed: 2D/3D physics, script API, asset animation, visual controls, bounded audio events, sandbox and bounded execution\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "World tests failed: " << error.what() << '\n';
        return 1;
    }
}
