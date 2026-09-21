-- Persistent counts, equipment and idempotent rewards. Definitions stay editable.
local Inventory = {}
function Inventory.count(item)
    return engine.state_get("inventory", {})[item] or 0
end
function Inventory.add(item, amount)
    local definition = engine.definitions().items[item]
    assert(definition, "Unknown item: " .. item)
    local items = engine.state_get("inventory", {})
    local nextCount = (items[item] or 0) + amount
    assert(amount == math.floor(amount), "Item amount must be an integer")
    if nextCount < 0 or nextCount > (definition.limit or 99) then return false end
    if nextCount == 0 then
        for _, equipped in pairs(engine.state_get("equipment", {})) do
            if equipped == item then return false end
        end
    end
    items[item] = nextCount
    engine.state_set("inventory", items)
    return true
end
function Inventory.equipped(slot)
    return engine.state_get("equipment", {})[slot]
end
function Inventory.equip(slot, item)
    local definition = engine.definitions().items[item]
    assert(definition, "Unknown item: " .. item)
    if definition.slot ~= slot or Inventory.count(item) < 1 then return false end
    local equipment = engine.state_get("equipment", {})
    for other, equipped in pairs(equipment) do
        if equipped == item and other ~= slot then return false end
    end
    equipment[slot] = item
    engine.state_set("equipment", equipment)
    return true
end
function Inventory.unequip(slot)
    local equipment = engine.state_get("equipment", {})
    local previous = equipment[slot]
    equipment[slot] = nil
    engine.state_set("equipment", equipment)
    return previous
end
function Inventory.grant(rewardId, rewards)
    assert(type(rewardId) == "string" and #rewardId > 0, "A stable reward ID is required")
    local claimed = engine.state_get("inventory_rewards", {})
    if claimed[rewardId] then return false, "claimed" end
    local items = engine.state_get("inventory", {})
    for item, amount in pairs(rewards) do
        local definition = engine.definitions().items[item]
        assert(definition and amount > 0 and amount == math.floor(amount), "Invalid reward item/count")
        local nextCount = (items[item] or 0) + amount
        if nextCount > (definition.limit or 99) then return false, "capacity" end
        items[item] = nextCount
    end
    claimed[rewardId] = true
    engine.state_set("inventory", items)
    engine.state_set("inventory_rewards", claimed)
    return true
end
return Inventory
