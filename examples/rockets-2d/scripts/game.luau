local t = 0
local cooldown = 0
local shots = {}
local defeated = {}
local kills = 0
local xs = {-8, -4, 0, 4, 8}
local function tell()
    engine.message("STAR PATROL  /  A-D: steer   W-S: altitude   SPACE: fire   /   Aliens: " .. kills .. "/5")
end
return {
    start = function()
        tell()
        for i=1,6 do shots[i] = false end
    end,
    update = function(dt)
        t += dt
        local x,y,z = engine.position("rocket")
        x = math.clamp(x + engine.input("move_x")*7*dt, -11, 11)
        y = math.clamp(y - engine.input("move_z")*5*dt, -8, -3)
        engine.set_position("rocket", x, y, z)
        engine.set_rotation("rocket", 0, 0, -engine.input("move_x")*12)
        cooldown -= dt
        if engine.input("fire") and cooldown <= 0 and kills < 5 then
            for i=1,6 do
                if not shots[i] then
                    shots[i] = true
                    engine.set_position("laser_"..i, x, y+1.4, z)
                    engine.set_visible("laser_"..i, true)
                    engine.play_sound("laser")
                    cooldown = 0.23
                    break
                end
            end
        end
        for j=1,5 do
            if not defeated[j] then
                engine.set_position("alien_"..j, xs[j], 4.4+math.sin(t*1.8+j)*0.35, 0)
                engine.set_rotation("alien_"..j, 0, math.sin(t+j)*14, math.sin(t+j)*5)
            end
        end
        for i=1,6 do
            if shots[i] then
                local id = "laser_"..i
                local sx,sy,sz = engine.position(id)
                engine.set_position(id, sx, sy+18*dt, sz)
                if sy > 9 then shots[i]=false; engine.set_visible(id,false) end
                for j=1,5 do
                    if shots[i] and not defeated[j] and engine.distance(id,"alien_"..j) < 1.35 then
                        defeated[j]=true
                        engine.collect("alien_"..j)
                        shots[i]=false
                        engine.set_visible(id,false)
                        kills += 1
                        tell()
                        if kills == 5 then
                            engine.win()
                            engine.message("SECTOR CLEAR!  /  Rocket, aliens, animation, music and sound: authored entirely as text.  R: replay")
                        end
                    end
                end
            end
        end
    end
}
