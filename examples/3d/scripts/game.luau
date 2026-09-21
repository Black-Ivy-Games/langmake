-- Lantern Court: a compact courtyard with five hidden-in-plain-sight lights.
local orbs = { "orb_west", "orb_north", "orb_east", "orb_south", "orb_altar" }
local collected = {}
local won = false
local jumpWasHeld = false

return {
    start = function()
        engine.message("LANTERN COURT  |  WASD or arrows to explore. Space to jump. Gather 5 lanterns and return to the central beacon.")
    end,
    update = function(dt)
        local xAxis = engine.input("move_x")
        local zAxis = engine.input("move_z")
        local length = math.sqrt(xAxis * xAxis + zAxis * zAxis)
        if length > 1 then
            xAxis /= length
            zAxis /= length
        end
        engine.move_player(xAxis * 5.5, zAxis * 5.5)
        local jumpHeld = engine.input("jump")
        if jumpHeld and not jumpWasHeld then
            engine.jump(8.5)
        end
        jumpWasHeld = jumpHeld

        local x, y, z = engine.position("player")
        if y < -4 then
            engine.set_position("player", -7, 1.2, -7)
            engine.message("Returned to the courtyard entrance. Your lanterns are safe.")
        end

        for _, id in ipairs(orbs) do
            if not collected[id] and engine.distance("player", id) < 1.5 then
                collected[id] = true
                engine.collect(id)
                local count = engine.collected()
                if count == #orbs then
                    engine.set_color("beacon", 0.35, 1, 0.8)
                    engine.message("All five lanterns found. Return to the tall cyan beacon in the center!")
                else
                    engine.message("Lanterns: " .. tostring(count) .. " / 5")
                end
            end
        end
        if not won and engine.collected() == #orbs and engine.distance("player", "beacon") < 2.3 then
            won = true
            engine.win()
            engine.message("LANTERN COURT COMPLETE!  The beacon is lit. Explore, or press R to restart.")
        end
    end,
}
