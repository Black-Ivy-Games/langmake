-- Camera and actor tracks use simulation ticks and inspectable engine state.
local Cinematics = {}
function Cinematics.play(id)
    assert(engine.definitions().cinematics[id], "Unknown cinematic: " .. id)
    engine.state_set("cinematic", {id=id, started=engine.tick(), active=true, camera=engine.camera_get()})
end
function Cinematics.active()
    return engine.state_get("cinematic", {}).active == true
end
function Cinematics.update()
    local current = engine.state_get("cinematic", {})
    if not current.active then return false end
    local shots = engine.definitions().cinematics[current.id].shots
    local tick = engine.tick() - current.started
    local offset = 0
    for index, shot in ipairs(shots) do
        if tick < offset + shot.ticks then
            engine.camera_set({mode="fixed",follow="",position=shot.position,target=shot.target,smoothing=0,obstruction=false})
            local progress = math.clamp((tick-offset)/math.max(1,shot.ticks-1),0,1)
            for _, actor in ipairs(shot.actors or {}) do
                if engine.exists(actor.id) then
                    if actor.from and actor.to then
                        local p = {}
                        for axis=1,3 do p[axis] = actor.from[axis]+(actor.to[axis]-actor.from[axis])*progress end
                        engine.set_position(actor.id,p[1],p[2],p[3])
                    end
                    if actor.from_rotation and actor.to_rotation then
                        local r = {}
                        for axis=1,3 do r[axis] = actor.from_rotation[axis]+(actor.to_rotation[axis]-actor.from_rotation[axis])*progress end
                        engine.set_rotation(actor.id,r[1],r[2],r[3])
                    end
                    if actor.animation and current.shot ~= index then engine.play_animation(actor.id,actor.animation,true,.1) end
                end
            end
            if current.shot ~= index then current.shot=index; engine.state_set("cinematic",current) end
            return true
        end
        offset += shot.ticks
    end
    current.active = false
    current.completed = true
    engine.state_set("cinematic", current)
    engine.camera_set(current.camera)
    return false
end
return Cinematics
