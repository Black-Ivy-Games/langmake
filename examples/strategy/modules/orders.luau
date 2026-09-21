-- Stagger path requests to keep large group orders within a script tick budget.
local Orders = {}
function Orders.select(group, ids)
    assert(#ids <= 256,"Selection exceeds navigation capacity")
    local selected, seen={},{}
    for _,id in ipairs(ids) do
        if engine.exists(id) and not seen[id] then table.insert(selected,id);seen[id]=true end
    end
    engine.state_set("selection:"..group,selected)
end
function Orders.selected(group)
    local result={}
    for _,id in ipairs(engine.state_get("selection:"..group,{})) do
        if engine.exists(id) then table.insert(result,id) end
    end
    return result
end
function Orders.formation(group, columns, spacing)
    if columns ~= nil then
        assert(columns>=1 and columns<=32 and columns==math.floor(columns),"Formation columns must be 1..32")
        assert(spacing>0 and spacing<=10,"Formation spacing must be (0,10]")
        engine.state_set("formation:"..group,{columns=columns,spacing=spacing})
    end
    return engine.state_get("formation:"..group,{columns=16,spacing=.48})
end
local function queue(group, ids, x, y, z, task)
    assert(#ids <= 256,"Order exceeds navigation capacity")
    local formation=Orders.formation(group)
    engine.state_set("order:" .. group, {ids=ids,index=1,x=x,y=y,z=z,task=task or "move",columns=formation.columns,spacing=formation.spacing})
end
function Orders.issue(group, ids, x, y, z, task)
    engine.state_set("follow:"..group,nil)
    queue(group,ids,x,y,z,task)
end
function Orders.assign(group, x, y, z, task)
    Orders.issue(group,Orders.selected(group),x,y,z,task)
end
function Orders.follow(group, leader)
    assert(engine.exists(leader),"Unknown group leader")
    engine.state_set("follow:"..group,{leader=leader,next_tick=engine.tick()})
end
function Orders.recall(group, leader)
    local x,y,z=engine.position(leader)
    Orders.assign(group,x,y,z,"recall")
end
function Orders.update(group)
    local following=engine.state_get("follow:"..group)
    if following and engine.tick()>=following.next_tick then
        if not engine.exists(following.leader) then engine.state_set("follow:"..group,nil)
        else
            local x,y,z=engine.position(following.leader)
            queue(group,Orders.selected(group),x,y,z,"follow")
            following.next_tick=engine.tick()+30
            engine.state_set("follow:"..group,following)
        end
    end
    local order = engine.state_get("order:" .. group)
    if not order then return end
    for _ = 1, 8 do
        local id = order.ids[order.index]
        if not id then break end
        if engine.exists(id) then
            local offset = order.index - 1
            local x = order.x + (offset % order.columns) * order.spacing
            local z = order.z + math.floor(offset / order.columns) * order.spacing
            engine.navigate(id, x, order.y, z, 3.5)
            engine.state_set("assignment:" .. id, order.task)
            engine.play_animation(id,"walk",false,0.12)
        end
        order.index += 1
    end
    engine.state_set("order:" .. group, order)
end
return Orders
