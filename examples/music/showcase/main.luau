local function refresh()
    engine.ui_set("cue", engine.state_get("cue", "storm-gate"))
    engine.ui_set("layer", engine.state_get("drums", true) and "Percussion present" or "Percussion fading out at next bar")
    engine.ui_set("count", "Cue changes: " .. engine.state_get("changes", 0) .. "  |  Chimes: " .. engine.state_get("chimes", 0))
end
local function select(cue)
    engine.state_set("cue", cue)
    engine.state_set("drums", true)
    engine.state_set("changes", engine.state_get("changes", 0) + 1)
    engine.play_music(cue)
end
return {
    start = refresh,
    on_load = refresh,
    update = function(dt)
        if engine.input_pressed("quiet") then select("lanterns") end
        if engine.input_pressed("adventure") then select("storm-gate") end
        if engine.input_pressed("playful") then select("clockwork-market") end
        if engine.input_pressed("drums") then
            local cue = engine.state_get("cue", "storm-gate")
            if cue ~= "storm-gate" then select("storm-gate") end
            local enabled = not engine.state_get("drums", true)
            engine.state_set("drums", enabled)
            engine.set_music_layer("drums", enabled and 1 or 0, 0.35, "bar")
        end
        if engine.input_pressed("chime") then
            engine.play_sound("chime")
            engine.state_set("chimes", engine.state_get("chimes", 0) + 1)
        end
        if engine.input_pressed("save") then engine.save_game("music-room") end
        if engine.input_pressed("load") then engine.load_game("music-room") end
        refresh()
    end
}
