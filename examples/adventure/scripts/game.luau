local Inventory, Quests, Dialogue = require("inventory"), require("quests"), require("dialogue")
local Combat, FSM, Cinematics = require("combat"), require("fsm"), require("cinematics")
local function refresh()
    engine.ui_set("title","Signal Orchard")
    engine.ui_set("controls","WASD move | E talk/use | 1/2 dialogue | Space strike | F6 save | F9 load")
    engine.ui_set("inventory","Coils: " .. Inventory.count("coil") .. " | Tool: " .. (Inventory.equipped("hand") or "none"))
    engine.ui_set("health","Integrity: " .. Combat.health("player",10))
    engine.ui_set("healthbar",{value=Combat.health("player",10),max=10})
    local objective = engine.scene() == "orchard" and "Talk to Keeper Ada, find the coil, clear the sentinel, enter the relay." or "Use E near the relay to restore the orchard signal."
    if Quests.get("signal").completed then objective = "The orchard is singing again. Return to Ada or explore." end
    engine.ui_set("objective",objective)
    engine.ui_set("status",engine.state_get("notice","Keeper Ada waits beside the old orchard path."))
    Dialogue.refresh()
end
local function initialize()
    if engine.state_get("hp:player") == nil then engine.state_set("hp:player",10) end
    if engine.state_get("hp:sentinel") == nil then engine.state_set("hp:sentinel",3) end
    engine.camera_set({mode="follow",follow="player",offset={0,6,10},smoothing=8,obstruction=true})
    refresh()
end
return {start=initialize,on_load=refresh,update=function(dt)
    if engine.input_pressed("save") then engine.save_game("journey"); engine.state_set("notice","Save requested: journey") end
    if engine.input_pressed("load") then engine.load_game("journey") end
    if Dialogue.active() then engine.move_player(0,0); Dialogue.update(); refresh(); return end
    if Cinematics.update() then engine.move_player(0,0); return end
    local mx,mz = engine.input("move_x"),engine.input("move_z")
    local moving = math.abs(mx)+math.abs(mz) > .01
    engine.move_player(mx*3,mz*3)
    if not engine.state_get("attacking",false) then engine.play_animation("player",moving and "walk" or "idle",false,.12) end
    if Inventory.count("tuner") > 0 and not Inventory.equipped("hand") then Inventory.equip("hand","tuner") end
    if engine.input_pressed("attack") and Inventory.equipped("hand") == "tuner" then
        engine.play_animation("player","attack",true,.08)
        engine.state_set("attacking",true); engine.state_set("attack_started",engine.tick())
    end
    for _,event in ipairs(engine.animation_events("player")) do
        if event.name == "strike" then
            local target,killed = Combat.strike("player","enemy",2.0,1,20)
            if target then engine.set_color(target,1,.55,.3) end
            if killed then engine.destroy(target); engine.state_set("sentinel_down",true); engine.state_set("notice","Sentinel safely powered down.") end
        end
    end
    if engine.state_get("attacking",false) and engine.tick()-engine.state_get("attack_started",0)>28 then engine.state_set("attacking",false) end
    if engine.state_get("accepted",false) and Quests.get("signal").stage == "unstarted" then Quests.advance("signal","find_coil") end
    if engine.scene() == "orchard" then
        if engine.exists("coil") and engine.distance("player","coil") < 1.2 then
            Inventory.add("coil",1); engine.destroy("coil"); Quests.advance("signal","clear_path"); engine.state_set("notice","A relay coil added to inventory.")
        end
        if engine.exists("sentinel") then
            if engine.distance("player","sentinel") < 1.6 then
                FSM.enter("sentinel","warning")
                if FSM.elapsed("sentinel") > 2 then Combat.damage("player",1); FSM.enter("sentinel","recover") end
            elseif FSM.get("sentinel").state ~= "idle" then FSM.enter("sentinel","idle") end
        end
        if engine.input_pressed("interact") then
            if engine.distance("player","ada") < 2 then Dialogue.open(Quests.get("signal").completed and "thanks" or "hello")
            elseif engine.distance("player","gate") < 2 then
                if Inventory.count("coil")>0 and engine.state_get("accepted",false) and engine.state_get("sentinel_down",false) then engine.change_scene("relay")
                else engine.state_set("notice","The relay needs Ada's request, one coil and a clear path.") end
            end
        end
    else
        if engine.input_pressed("interact") and engine.distance("player","relay") < 2 then
            if not Quests.get("signal").completed and Inventory.add("coil",-1) then
                Quests.complete("signal"); engine.set_color("relay",.3,1,.65); engine.state_set("notice","The orchard signal is restored."); Cinematics.play("restored"); engine.message("Signal restored"); engine.win()
            end
        elseif engine.input_pressed("interact") and engine.distance("player","return") < 2 then engine.change_scene("orchard") end
    end
    if Combat.health("player",10)<=0 then engine.move_player(0,0); engine.state_set("notice","Integrity depleted. F9 loads your last journey save.") end
    refresh()
end}
