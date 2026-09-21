local Inventory,Quests,Dialogue=require("inventory"),require("quests"),require("dialogue")
local function refresh()
    engine.ui_set("title","Lantern Post")
    engine.ui_set("objective",engine.scene()=="garden" and "The garden lantern is lit. E at the blue door returns to the village." or "Talk to the keeper, collect the star, then use the post on the right.")
    engine.ui_set("inventory","Stars: "..Inventory.count("star"))
    engine.ui_set("health",Quests.get("post").completed and "Lantern lit" or "Lantern waiting")
    engine.ui_set("healthbar",{value=Quests.get("post").completed and 1 or 0,max=1})
    engine.ui_set("status",Quests.get("post").completed and "Lantern restored." or "Find a light for the old post.")
    engine.ui_set("controls","WASD move | E talk/use | 1/2 choices | F6 save | F9 load")
    Dialogue.refresh()
end
return {start=refresh,on_load=refresh,update=function(dt)
    if engine.input_pressed("save") then engine.save_game("lantern") end
    if engine.input_pressed("load") then engine.load_game("lantern") end
    if Dialogue.active() then engine.set_velocity("player",0,0,0);Dialogue.update();refresh();return end
    engine.set_velocity("player",engine.input("move_x")*3,engine.input("move_z")*3,0)
    if engine.exists("star") and engine.distance("player","star")<1 then Inventory.add("star",1);engine.destroy("star") end
    if engine.input_pressed("interact") then
        if engine.scene()=="garden" then
            if engine.distance("player","return")<2 then engine.change_scene("village") end
        elseif engine.distance("player","keeper")<2 then Dialogue.open("hello")
        elseif engine.distance("player","post")<2 then
            if Quests.get("post").completed then engine.change_scene("garden")
            elseif engine.state_get("accepted",false) and Inventory.add("star",-1) then
                Quests.complete("post");engine.set_color("post",1,.85,.25);engine.message("Lantern restored");engine.change_scene("garden")
            end
        end
    end
    if Quests.get("post").completed then engine.win() end
    refresh()
end}
