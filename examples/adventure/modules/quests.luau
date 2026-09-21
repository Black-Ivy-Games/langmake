-- Every quest transition lives in engine state and survives scene/save changes.
local Quests = {}
function Quests.get(id)
    return engine.state_get("quests", {})[id] or {stage="unstarted", completed=false}
end
function Quests.advance(id, stage)
    assert(engine.definitions().quests[id], "Unknown quest: " .. id)
    local quests = engine.state_get("quests", {})
    local quest = quests[id] or {stage="unstarted", completed=false}
    if quest.completed then return false end
    quest.stage = stage
    quests[id] = quest
    engine.state_set("quests", quests)
    return true
end
function Quests.complete(id)
    local quests = engine.state_get("quests", {})
    local quest = Quests.get(id)
    if quest.completed then return false end
    quest.stage = "complete"
    quest.completed = true
    quests[id] = quest
    engine.state_set("quests", quests)
    return true
end
return Quests
