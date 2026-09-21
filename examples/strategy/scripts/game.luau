local Orders, Quests, Inventory = require("orders"),require("quests"),require("inventory")
local function refresh()
    local units = engine.state_get("workers",{})
    engine.ui_set("title","Mosslight Assembly")
    engine.ui_set("objective","C: send the team around the wall to gather light. V: bring it home.")
    engine.ui_set("inventory","Delivered light: " .. Inventory.count("light"))
    engine.ui_set("health","Workers: " .. #units .. " / 160 | Selected: " .. #Orders.selected("team"))
    engine.ui_set("healthbar",{value=#units,max=160})
    engine.ui_set("status",engine.state_get("notice","128 animated workers await your first group order."))
    engine.ui_set("dialogue-panel",{visible=false})
    engine.ui_set("controls","1/2/3 select all/halves | C gather | V return | B follow | J recall | N spawn | H dismiss | F6/F9 save/load")
end
local function add()
    local units = engine.state_get("workers",{})
    if #units>=160 then return end
    local i=#units
    local id=engine.spawn("worker",{position={-10+(i%16)*.48,.45,-2+math.floor(i/16)*.48}})
    engine.set_color(id,.55+engine.random()*.25,.78,.45+engine.random()*.2)
    table.insert(units,id); engine.state_set("workers",units)
end
return {start=function()
    if engine.state_get("workers") == nil then engine.state_set("workers",{});for _=1,128 do add() end end
    Orders.select("team",engine.state_get("workers",{}))
    refresh()
end,on_load=refresh,update=function(dt)
    if engine.input_pressed("save") then engine.save_game("assembly");engine.state_set("notice","Save requested: assembly") end
    if engine.input_pressed("load") then engine.load_game("assembly") end
    engine.move_player(engine.input("move_x")*4,engine.input("move_z")*4)
    if engine.input_pressed("spawn") then add() end
    if engine.input_pressed("dismiss") then local units=engine.state_get("workers",{});local id=table.remove(units);if id then engine.destroy(id) end;engine.state_set("workers",units) end
    local units=engine.state_get("workers",{})
    if engine.input_pressed("select_all") then Orders.select("team",units) end
    if engine.input_pressed("select_first") or engine.input_pressed("select_second") then
        local half={};local first=engine.input_pressed("select_first")
        for i,id in ipairs(units) do if (first and i<=math.ceil(#units/2)) or (not first and i>math.ceil(#units/2)) then table.insert(half,id) end end
        Orders.select("team",half)
    end
    if engine.input_pressed("command") then Orders.assign("team",4,.1,-2,"gather");engine.state_set("notice","Finding paths around the central wall...") end
    if engine.input_pressed("recall") then Orders.assign("team",-10,.1,-2,"return");engine.state_set("notice","Return order issued.") end
    if engine.input_pressed("follow") then Orders.follow("team","player") end
    if engine.input_pressed("regroup") then Orders.recall("team","player") end
    Orders.update("team")
    local arrived=0
    for _,id in ipairs(units) do
        if engine.exists(id) and engine.navigation_status(id).status == "arrived" then
            arrived+=1;engine.play_animation(id,"idle",false,.15)
            if engine.state_get("assignment:"..id,"")=="gather" and not engine.state_get("cargo:"..id,false) then
                engine.state_set("cargo:"..id,true);engine.set_opacity(id,.82);engine.set_visual_scale(id,1.1,1.1,1.1)
            elseif engine.state_get("assignment:"..id,"")=="return" and engine.state_get("cargo:"..id,false) then
                engine.state_set("cargo:"..id,false);Inventory.add("light",1);engine.set_opacity(id,1);engine.set_visual_scale(id,1,1,1)
            end
        end
    end
    engine.state_set("arrived",arrived)
    if Inventory.count("light")>=100 then Quests.complete("assembly");engine.message("100 lights delivered");engine.win() end
    refresh()
end}
