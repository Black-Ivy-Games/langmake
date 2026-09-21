-- Skyway: a small jumping course. All gameplay uses the public engine API.
local orbs = { "orb_1", "orb_2", "orb_3", "orb_4", "orb_5" }
local collected = {}
local won = false
local jumpWasHeld = false

return {
    start = function()
        engine.message("SKYWAY  |  A / D or arrows to move. Space to jump. Find 5 sun sparks, then reach the cyan beacon.")
    end,
    update = function(dt)
        engine.move_player(engine.input("move_x") * 6.5, 0)
        local jumpHeld = engine.input("jump")
        if jumpHeld and not jumpWasHeld then
            engine.jump(10.5)
        end
        jumpWasHeld = jumpHeld

        local x, y, z = engine.position("player")
        if y < -2.3 then
            engine.set_position("player", -12, 1.2, 0)
            engine.message("Back on the launch pad. Your collected sparks are safe!")
        end

        for _, id in ipairs(orbs) do
            if not collected[id] and engine.distance("player", id) < 1.2 then
                collected[id] = true
                engine.collect(id)
                local count = engine.collected()
                if count == #orbs then
                    engine.set_color("beacon", 0.35, 1, 0.8)
                    engine.message("All 5 sun sparks found! Reach the beacon at the far right.")
                else
                    engine.message("Sun sparks: " .. tostring(count) .. " / 5")
                end
            end
        end
        if not won and engine.collected() == #orbs and engine.distance("player", "beacon") < 1.5 then
            won = true
            engine.win()
            engine.message("SKYWAY COMPLETE!  All five sparks delivered. Explore, or press R to restart.")
        end
    end,
}
