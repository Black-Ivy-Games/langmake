local Combat = {}
function Combat.health(id, fallback)
    return engine.state_get("hp:" .. id, fallback or 1)
end
function Combat.damage(id, amount)
    local health = math.max(0, Combat.health(id) - amount)
    engine.state_set("hp:" .. id, health)
    return health == 0
end
function Combat.target(source, role, radius)
    local x,y,z = engine.position(source)
    local hits = engine.overlap(x,y,z,radius,{role=role,exclude={source},limit=64})
    local selected, distance = nil, math.huge
    for _, id in ipairs(hits.entities) do
        local nextDistance = engine.distance(source,id)
        if nextDistance < distance then selected = id; distance = nextDistance end
    end
    return selected
end
function Combat.strike(source, role, radius, amount, cooldownTicks)
    local previous = engine.state_get("attack:" .. source, -100000)
    if engine.tick() - previous < cooldownTicks then return nil, false end
    engine.state_set("attack:" .. source, engine.tick())
    local target = Combat.target(source, role, radius)
    if not target then return nil, false end
    return target, Combat.damage(target,amount)
end
return Combat
