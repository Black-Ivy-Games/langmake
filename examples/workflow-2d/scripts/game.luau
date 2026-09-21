local balance = engine.definitions().balance
local function refresh()
    local count = engine.state_get("collected", 0)
    local delivered = engine.state_get("delivered", false)
    local paused = engine.state_get("paused", false)
    engine.ui_set("count", "Seeds: " .. count .. "/" .. balance.seed_count)
    engine.ui_set("progress", {value = count, max = balance.seed_count})
    engine.ui_set("objective", delivered and "Garden delivered. Thank you!" or "Gather seeds with LIGHT; return to the blue dock.")
    engine.ui_set("status", engine.state_get("message", ""))
    engine.ui_set("pause-panel", {visible = paused})
    engine.ui_set("lamp-button", {visible = not paused})
    engine.ui_set("pause-button", paused and "Continue" or "Pause")
    engine.set_color("dock", delivered and 0.4 or 0.23, delivered and 0.85 or 0.43, delivered and 0.55 or 0.58)
end

return {
    start = function()
        engine.set_audio_bus("music", balance.music_gain, 0, false)
        refresh()
    end,
    -- Restoring declared state never awards a second delivery reward.
    on_load = refresh,
    update = function(dt)
        if engine.input_pressed("pause") then
            engine.state_set("paused", not engine.state_get("paused", false))
        end
        if engine.input_pressed("save") then engine.save_game("garden") end
        if engine.input_pressed("load") then engine.load_game("garden") end
        if engine.state_get("paused", false) then
            engine.set_velocity("player", 0, 0, 0)
            refresh()
            return
        end
        local x, y = engine.position("player")
        local vx, vy = engine.input("move_x") * balance.speed, -engine.input("move_z") * balance.speed
        if (x <= -6 and vx < 0) or (x >= 6 and vx > 0) then vx = 0 end
        if (y <= -2 and vy < 0) or (y >= 2 and vy > 0) then vy = 0 end
        engine.set_velocity("player", vx, vy, 0)
        local lamp = engine.input("lamp") > 0
        engine.set_color("player", lamp and 1 or 0.65, 1, lamp and 0.55 or 1)
        if lamp then
            engine.state_set("held_ticks", engine.state_get("held_ticks", 0) + 1)
            for i = 1, balance.seed_count do
                local id = "seed" .. i
                if engine.exists(id) and engine.distance("player", id) < balance.collect_radius then
                    engine.destroy(id)
                    engine.state_set("collected", engine.state_get("collected", 0) + 1)
                    engine.state_set("message", "Seed collected. A full bag still needs delivery at the dock.")
                end
            end
        end
        if engine.input_released("lamp") then
            engine.state_set("release_count", engine.state_get("release_count", 0) + 1)
        end
        if engine.state_get("collected", 0) == balance.seed_count and not engine.state_get("delivered", false)
            and engine.distance("player", "dock") < balance.dock_radius then
            engine.state_set("delivered", true)
            engine.state_set("reward_count", engine.state_get("reward_count", 0) + 1)
            engine.state_set("message", "Delivery complete. Playtest movement and the LIGHT button, then share feedback.")
            engine.win()
        end
        refresh()
    end
}
