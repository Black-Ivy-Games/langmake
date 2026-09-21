-- Branching nodes and conditions are JSON; progress is an inspectable state key.
local Dialogue = {}
local Inventory = require("inventory")
local function node()
    local state = engine.state_get("dialogue", {})
    if not state.active then return nil end
    return engine.definitions().dialogue[state.node]
end
function Dialogue.active()
    return engine.state_get("dialogue", {}).active == true
end
function Dialogue.choices()
    local current = node()
    local choices = {}
    if current then
        for _, choice in ipairs(current.choices or {}) do
            local condition = choice.condition
            if not condition or engine.state_get(condition.flag) == condition.value then
                table.insert(choices, choice)
            end
        end
    end
    return choices
end
function Dialogue.refresh()
    local current = node()
    local choices = Dialogue.choices()
    engine.ui_set("dialogue-panel", {visible=current ~= nil})
    engine.ui_set("speaker", current and current.speaker or "")
    engine.ui_set("dialogue-line", current and current.text or "")
    for i = 1, 2 do
        engine.ui_set("choice" .. i, {text=choices[i] and choices[i].text or "", visible=choices[i] ~= nil})
    end
end
function Dialogue.open(id)
    assert(engine.definitions().dialogue[id], "Unknown dialogue node: " .. id)
    engine.state_set("dialogue", {node=id, active=true})
    Dialogue.refresh()
end
function Dialogue.choose(index)
    local choice = Dialogue.choices()[index]
    if not choice then return end
    if choice.rewards then
        local granted, reason = Inventory.grant(choice.reward_id, choice.rewards)
        if not granted and reason ~= "claimed" then return false end
    end
    if choice.flag then engine.state_set(choice.flag, choice.value) end
    if choice.next then Dialogue.open(choice.next)
    else engine.state_set("dialogue", {active=false}); Dialogue.refresh() end
end
function Dialogue.update()
    if not Dialogue.active() then return end
    if engine.input_pressed("choice2") or #engine.ui_event("choice2", "click") > 0 then
        Dialogue.choose(2)
    elseif engine.input_pressed("interact") or engine.input_pressed("choice1") or #engine.ui_event("choice1", "click") > 0 then
        Dialogue.choose(1)
    end
end
return Dialogue
