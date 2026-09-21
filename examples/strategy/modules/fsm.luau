-- Small reusable finite-state machine; no VM-local progress to lose on reload.
local FSM = {}
function FSM.get(id)
    return engine.state_get("fsm:" .. id, {state="idle", entered=engine.tick()})
end
function FSM.enter(id, state)
    local current = FSM.get(id)
    if current.state == state then return false end
    engine.state_set("fsm:" .. id, {state=state, entered=engine.tick()})
    return true
end
function FSM.elapsed(id)
    return (engine.tick() - FSM.get(id).entered) / 60
end
function FSM.after(id, ticks)
    assert(ticks >= 0 and ticks == math.floor(ticks), "Timer uses nonnegative integer ticks")
    engine.state_set("timer:"..id,engine.tick()+ticks)
end
function FSM.due(id, consume)
    local deadline=engine.state_get("timer:"..id)
    if deadline == nil or engine.tick() < deadline then return false end
    if consume ~= false then engine.state_set("timer:"..id,nil) end
    return true
end
return FSM
