"""Regenerate only the three engine-owned gameplay reference projects.

All art, animation, music, story and behavior are authored here as text. No DCC
tools or downloaded content are needed. Games are technical reference slices;
their creative quality still needs the human developer's playtesting.
"""
from pathlib import Path
import json
import math
import argparse
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MODULES = {}
MODULES['inventory'] = r'''
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
'''
MODULES['quests'] = r'''
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
'''
MODULES['dialogue'] = r'''
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
'''
MODULES['fsm'] = r'''
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
'''
MODULES['combat'] = r'''
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
'''
MODULES['cinematics'] = r'''
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
'''
MODULES['orders'] = r'''
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
'''

STYLE = '''
body { display: block; width: 100%; height: 100%; margin: 0px; font-family: Langmake; font-size: 20px; color: #f4f5ef; }
.panel { position: absolute; left: 20px; top: 18px; width: 520px; padding: 16px; background: #11232aef; border: 2px #64aa95; }
h1 { display: block; font-size: 24px; color: #c7edaa; margin: 0px 0px 8px; }
p { display: block; margin: 5px 0px; }
progress { display: block; width: 250px; height: 14px; background: #243a42; }
progress fill { background: #a4d980; }
#dialogue-panel { top: auto; bottom: 90px; width: 620px; background: #172a38f5; }
#speaker { font-size: 23px; color: #f4cc79; }
#controls { position: absolute; bottom: 16px; left: 20px; width: 90%; padding: 12px; background: #0e202adf; }
button { display: block; margin: 8px 0px; padding: 8px 12px; background: #386857; color: #ffffff; }
button:hover { background: #4e896c; }
#status { color: #ffe08a; }
'''
HUD = '''<rml><head><title>Langmake gameplay reference</title><link type="text/rcss" href="hud.rcss"/></head><body>
<div class="panel"><h1 id="title">Reference game</h1><p id="objective">Explore</p><p id="inventory">Inventory</p><p id="health">Health</p><progress id="healthbar" value="10" max="10"/><p id="status">Ready</p></div>
<div class="panel" id="dialogue-panel"><p id="speaker"></p><p id="dialogue-line"></p><button id="choice1">Continue</button><button id="choice2">Ask</button></div>
<div id="controls"></div>
</body></rml>
'''

def write(root, path, value):
    if path == 'verification-input.json' and isinstance(value,list):
        value = {'events':value}
    if path == 'README.md':
        value = value.replace('langmake play <', 'langmake play --project <').replace('langmake.exe play examples/', 'langmake.exe play --project examples/')
    target = root / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(value, indent=2) + '\n' if not isinstance(value,str) else value.strip() + '\n', encoding='utf-8')

def robot():
    joints=[{'id':'root'}, {'id':'head','parent':'root','translation':[0,.36,0]},
            {'id':'arm','parent':'root','translation':[.32,.02,0]},
            {'id':'left','parent':'root','translation':[-.15,-.32,0]},
            {'id':'right','parent':'root','translation':[.15,-.32,0]}]
    parts=[{'id':'body','joint':'root','color':'#75CCA8','geometry':{'op':'box','size':[.5,.45,.35]}},
           {'id':'head','joint':'head','color':'#BCEBCA','geometry':{'op':'box','size':[.45,.3,.4]}},
           {'id':'visor','joint':'head','color':'#233744','geometry':{'op':'box','size':[.3,.09,.04],'translate':[0,.02,.21]}},
           {'id':'arm','joint':'arm','color':'#F0BD69','geometry':{'op':'box','size':[.16,.32,.18],'translate':[0,-.1,0]}},
           {'id':'left','joint':'left','color':'#43766B','geometry':{'op':'box','size':[.16,.26,.22]}},
           {'id':'right','joint':'right','color':'#43766B','geometry':{'op':'box','size':[.16,.26,.22]}}]
    def rotations(values):
        return [{'time':t,'value':[math.sin(angle/2),0,0,math.cos(angle/2)]} for t,angle in values]
    return {'schema_version':1,'joints':joints,'parts':parts,'default_animation':'idle','animations':{
        'idle':{'duration':1.5,'tracks':{'head':{'translation':[{'time':0,'value':[0,.36,0]},{'time':.75,'value':[0,.39,0]},{'time':1.5,'value':[0,.36,0]}]}}},
        'walk':{'duration':.6,'tracks':{'left':{'rotation':rotations([(0,-.45),(.3,.45),(.6,-.45)])},'right':{'rotation':rotations([(0,.45),(.3,-.45),(.6,.45)])}}},
        'attack':{'duration':.45,'loop':False,'tracks':{'arm':{'rotation':rotations([(0,0),(.15,-1.5),(.45,0)])}},'markers':[{'name':'strike','time':.15}]}}}

def entity(id,position,size=(1,1,1),**kw):
    value={'id':id,'position':position,'size':list(size),'body':'none','role':'decoration','color':[1,1,1,1]}
    value.update(kw)
    return value

def common(name,dimension='3d'):
    root=ROOT/'examples'/name
    for id,code in MODULES.items():write(root,'modules/'+id+'.luau',code)
    write(root,'assets/robot.model.json',robot())
    write(root,'ui/hud.rml',HUD);write(root,'ui/hud.rcss',STYLE)
    # A quiet original pad plus plucked arpeggio; synthesis stays text authored.
    score={'schema_version':1,'type':'music','bpm':100,'ppq':24,'length':192,'gain':.35,
           'instruments':{'bell':{'wave':'triangle','gain':.18,'attack':.005,'decay':.2,'sustain':.2,'release':.3}},
           'notes':[{'tick':i*12,'duration':10,'note':[60,64,67,72,67,64,62,67][i%8],'instrument':'bell'} for i in range(16)]}
    write(root,'assets/theme.music.json',score)
    project={'schema_version':2,'title':name,'dimension':dimension,'gravity':[0,-18 if dimension=='3d' else 0,0],
             'camera':{'position':[0,12,18] if dimension=='3d' else [0,0,20],'target':[0,0,0]},
             'script':'scripts/game.luau','entities':[],
             'modules':{id:'modules/'+id+'.luau' for id in MODULES},
             'assets':{'robot':{'type':'model','path':'assets/robot.model.json'},'theme':{'type':'music','path':'assets/theme.music.json'}},'music':'theme',
             'definitions':{},'ui':{'documents':{'hud':'ui/hud.rml'},'styles':{'hud':'ui/hud.rcss'}},
             'input':{'actions':{'interact':{'keys':['E'],'gamepad':'south'},'attack':{'keys':['Space'],'gamepad':'west'},'choice1':{'keys':['1']},'choice2':{'keys':['2']},'save':{'keys':['F6']},'load':{'keys':['F9']}}}}
    return root,project

ADVENTURE = r'''
local Inventory, Quests, Dialogue = require("inventory"), require("quests"), require("dialogue")
local Combat, FSM, Cinematics = require("combat"), require("fsm"), require("cinematics")
local function refresh()
    engine.ui_set("title","Signal Orchard")
    engine.ui_set("controls","WASD move | E talk/use | 1/2 dialogue | Space strike | F6 save | F9 load")
    engine.ui_set("inventory","Coils: " .. Inventory.count("coil") .. " | Tool: " .. (Inventory.equipped("hand") or "none"))
    engine.ui_set("health","Integrity: " .. Combat.health("player",10))
    engine.ui_set("healthbar",{value=Combat.health("player",10),max=10})
    local objective = engine.scene() == "orchard" and "Talk to Keeper Ada, find the coil, clear the sentinel, enter the relay." or "Use E near the relay to restore the orchard signal."
    if Quests.get("signal").completed then objective = "The orchard is singing again. Return to Ada or explore." end
    engine.ui_set("objective",objective)
    engine.ui_set("status",engine.state_get("notice","Keeper Ada waits beside the old orchard path."))
    Dialogue.refresh()
end
local function initialize()
    if engine.state_get("hp:player") == nil then engine.state_set("hp:player",10) end
    if engine.state_get("hp:sentinel") == nil then engine.state_set("hp:sentinel",3) end
    engine.camera_set({mode="follow",follow="player",offset={0,6,10},smoothing=8,obstruction=true})
    refresh()
end
return {start=initialize,on_load=refresh,update=function(dt)
    if engine.input_pressed("save") then engine.save_game("journey"); engine.state_set("notice","Save requested: journey") end
    if engine.input_pressed("load") then engine.load_game("journey") end
    if Dialogue.active() then engine.move_player(0,0); Dialogue.update(); refresh(); return end
    if Cinematics.update() then engine.move_player(0,0); return end
    local mx,mz = engine.input("move_x"),engine.input("move_z")
    local moving = math.abs(mx)+math.abs(mz) > .01
    engine.move_player(mx*3,mz*3)
    if not engine.state_get("attacking",false) then engine.play_animation("player",moving and "walk" or "idle",false,.12) end
    if Inventory.count("tuner") > 0 and not Inventory.equipped("hand") then Inventory.equip("hand","tuner") end
    if engine.input_pressed("attack") and Inventory.equipped("hand") == "tuner" then
        engine.play_animation("player","attack",true,.08)
        engine.state_set("attacking",true); engine.state_set("attack_started",engine.tick())
    end
    for _,event in ipairs(engine.animation_events("player")) do
        if event.name == "strike" then
            local target,killed = Combat.strike("player","enemy",2.0,1,20)
            if target then engine.set_color(target,1,.55,.3) end
            if killed then engine.destroy(target); engine.state_set("sentinel_down",true); engine.state_set("notice","Sentinel safely powered down.") end
        end
    end
    if engine.state_get("attacking",false) and engine.tick()-engine.state_get("attack_started",0)>28 then engine.state_set("attacking",false) end
    if engine.state_get("accepted",false) and Quests.get("signal").stage == "unstarted" then Quests.advance("signal","find_coil") end
    if engine.scene() == "orchard" then
        if engine.exists("coil") and engine.distance("player","coil") < 1.2 then
            Inventory.add("coil",1); engine.destroy("coil"); Quests.advance("signal","clear_path"); engine.state_set("notice","A relay coil added to inventory.")
        end
        if engine.exists("sentinel") then
            if engine.distance("player","sentinel") < 1.6 then
                FSM.enter("sentinel","warning")
                if FSM.elapsed("sentinel") > 2 then Combat.damage("player",1); FSM.enter("sentinel","recover") end
            elseif FSM.get("sentinel").state ~= "idle" then FSM.enter("sentinel","idle") end
        end
        if engine.input_pressed("interact") then
            if engine.distance("player","ada") < 2 then Dialogue.open(Quests.get("signal").completed and "thanks" or "hello")
            elseif engine.distance("player","gate") < 2 then
                if Inventory.count("coil")>0 and engine.state_get("accepted",false) and engine.state_get("sentinel_down",false) then engine.change_scene("relay")
                else engine.state_set("notice","The relay needs Ada's request, one coil and a clear path.") end
            end
        end
    else
        if engine.input_pressed("interact") and engine.distance("player","relay") < 2 then
            if not Quests.get("signal").completed and Inventory.add("coil",-1) then
                Quests.complete("signal"); engine.set_color("relay",.3,1,.65); engine.state_set("notice","The orchard signal is restored."); Cinematics.play("restored"); engine.message("Signal restored"); engine.win()
            end
        elseif engine.input_pressed("interact") and engine.distance("player","return") < 2 then engine.change_scene("orchard") end
    end
    if Combat.health("player",10)<=0 then engine.move_player(0,0); engine.state_set("notice","Integrity depleted. F9 loads your last journey save.") end
    refresh()
end}
'''

def adventure():
    root,p=common('adventure');p['title']='Signal Orchard';p['entry_scene']='orchard';p['scenes']={'orchard':'scenes/orchard.json','relay':'scenes/relay.json'}
    p['definitions']={'items':'data/items.json','quests':'data/quests.json','dialogue':'data/dialogue.json','cinematics':'data/cinematics.json'}
    write(root,'data/items.json',{'coil':{'name':'Relay coil','limit':9},'tuner':{'name':'Signal tuner','limit':1,'slot':'hand'}});write(root,'data/quests.json',{'signal':{'title':'Restore the signal'}})
    write(root,'data/dialogue.json',{
        'hello':{'speaker':'Keeper Ada','text':'The orchard stopped singing. Will you bring its signal back?','choices':[{'text':'1 / E: I will help.','flag':'accepted','value':True,'next':'mission','reward_id':'ada_tuner','rewards':{'tuner':1}},{'text':'2: What happened?','next':'history'}]},
        'history':{'speaker':'Keeper Ada','text':'Our sentinel mistook the repair crew for a storm. It only needs a gentle shutdown.','choices':[{'text':'1 / E: I can do that.','flag':'accepted','value':True,'next':'mission','reward_id':'ada_tuner','rewards':{'tuner':1}}]},
        'mission':{'speaker':'Keeper Ada','text':'Take my signal tuner. Collect the amber coil ahead, quiet the sentinel with three strikes, then use the relay door.','choices':[{'text':'1 / E: Understood.'}]},
        'thanks':{'speaker':'Keeper Ada','text':'There it is: the orchard song. What should we build here next?','choices':[{'text':'1 / E: Let me explore.'}]}})
    write(root,'data/cinematics.json',{'restored':{'shots':[{'ticks':60,'position':[5,5,8],'target':[2,2,0],'actors':[{'id':'signal_orb','from':[2,2,0],'to':[2,3,0]}]},{'ticks':90,'position':[-3,3,5],'target':[2,3,0],'actors':[{'id':'signal_orb','from':[2,3,0],'to':[2,4,0],'from_rotation':[0,0,0],'to_rotation':[0,180,0]}]}]}})
    def player(x):return entity('player',[x,1,0],[.7,1.8,.7],role='player',body='dynamic',shape='capsule',controller='character',asset='robot',animation='idle',visual_scale=[1.8,1,1.8])
    floor=entity('ground',[0,-.5,0],[22,1,14],body='static',color=[.12,.22,.20,1])
    orchard=[floor,player(-5),entity('ada',[-3,.9,1.5],[.7,1.8,.7],asset='robot',animation='idle',visual_scale=[1.8,1,1.8]),
        entity('coil',[0,.55,0],[.5,.5,.5],shape='sphere',color=[1,.7,.22,1]),
        entity('sentinel',[2.4,.9,0],[.8,1.8,.8],asset='robot',animation='idle',body='static',role='enemy',color=[.9,.4,.32,1],visual_scale=[1.7,1,1.7]),
        entity('gate',[6,1,0],[1,2,1],color=[.32,.5,.78,1])]
    relay=[floor,player(-2),entity('relay',[2,1,0],[1,2,1],body='static',color=[.65,.45,.18,1]),entity('signal_orb',[2,2,0],[.35,.35,.35],shape='sphere',color=[.5,1,.65,1]),entity('return',[-5,1,0],[1,2,1],color=[.3,.5,.8,1])]
    write(root,'scenes/orchard.json',{'entities':orchard});write(root,'scenes/relay.json',{'entities':relay})
    write(root,'scripts/game.luau',ADVENTURE);write(root,'project.json',p)
    write(root,'verification-input.json',[{'from':0,'to':35,'move_x':1},{'from':36,'to':37,'actions':{'interact':1}},{'from':38,'to':39,'actions':{'choice1':1}},{'from':40,'to':41,'actions':{'interact':1}},{'from':42,'to':147,'move_x':1},{'from':150,'to':151,'actions':{'attack':1}},{'from':185,'to':186,'actions':{'attack':1}},{'from':220,'to':221,'actions':{'attack':1}},{'from':250,'to':330,'move_x':1},{'from':332,'to':333,'actions':{'interact':1}},{'from':340,'to':390,'move_x':1},{'from':392,'to':393,'actions':{'interact':1}}])
    write(root,'README.md','''# Signal Orchard
An original small adventure reference: two areas, persistent branching dialogue,
quest, equipment and inventory modules, capsule movement, native skeletal animation,
marker-driven melee, camera/actor tracks, anchored HUD and player saves.

Run `langmake play --project <this-directory>` (or `build/langmake.exe play --project examples/adventure`
from the repository). WASD moves; E talks/uses; 1/2 choose dialogue; Space strikes.
F6 saves slot `journey`, F9 loads it after you have saved.

Talk to Ada on your right, collect the amber coil, hit the red sentinel three
times, then use the blue door. In the relay room, approach the amber machine and
press E. You can return through the blue marker afterward.

All character parts, clips and music are text-authored. Modules in `modules/`
are editable building blocks, not fixed engine story rules. Progress uses engine
state; `on_load` restores UI without granting items again. Saves require a matching
source revision. Existing project edits restart preview gameplay.

Technical verification uses the recorded input timeline and state/capture
commands. Human playtesting remains required: Is interaction range clear? Does
movement/camera feel comfortable? What should the next story choice change?
''')

STRATEGY=r'''
local Orders, Quests, Inventory = require("orders"),require("quests"),require("inventory")
local function refresh()
    local units = engine.state_get("workers",{})
    engine.ui_set("title","Mosslight Assembly")
    engine.ui_set("objective","C: send the team around the wall to gather light. V: bring it home.")
    engine.ui_set("inventory","Delivered light: " .. Inventory.count("light"))
    engine.ui_set("health","Workers: " .. #units .. " / 160 | Selected: " .. #Orders.selected("team"))
    engine.ui_set("healthbar",{value=#units,max=160})
    engine.ui_set("status",engine.state_get("notice","128 animated workers await your first group order."))
    engine.ui_set("dialogue-panel",{visible=false})
    engine.ui_set("controls","1/2/3 select all/halves | C gather | V return | B follow | J recall | N spawn | H dismiss | F6/F9 save/load")
end
local function add()
    local units = engine.state_get("workers",{})
    if #units>=160 then return end
    local i=#units
    local id=engine.spawn("worker",{position={-10+(i%16)*.48,.45,-2+math.floor(i/16)*.48}})
    engine.set_color(id,.55+engine.random()*.25,.78,.45+engine.random()*.2)
    table.insert(units,id); engine.state_set("workers",units)
end
return {start=function()
    if engine.state_get("workers") == nil then engine.state_set("workers",{});for _=1,128 do add() end end
    Orders.select("team",engine.state_get("workers",{}))
    refresh()
end,on_load=refresh,update=function(dt)
    if engine.input_pressed("save") then engine.save_game("assembly");engine.state_set("notice","Save requested: assembly") end
    if engine.input_pressed("load") then engine.load_game("assembly") end
    engine.move_player(engine.input("move_x")*4,engine.input("move_z")*4)
    if engine.input_pressed("spawn") then add() end
    if engine.input_pressed("dismiss") then local units=engine.state_get("workers",{});local id=table.remove(units);if id then engine.destroy(id) end;engine.state_set("workers",units) end
    local units=engine.state_get("workers",{})
    if engine.input_pressed("select_all") then Orders.select("team",units) end
    if engine.input_pressed("select_first") or engine.input_pressed("select_second") then
        local half={};local first=engine.input_pressed("select_first")
        for i,id in ipairs(units) do if (first and i<=math.ceil(#units/2)) or (not first and i>math.ceil(#units/2)) then table.insert(half,id) end end
        Orders.select("team",half)
    end
    if engine.input_pressed("command") then Orders.assign("team",4,.1,-2,"gather");engine.state_set("notice","Finding paths around the central wall...") end
    if engine.input_pressed("recall") then Orders.assign("team",-10,.1,-2,"return");engine.state_set("notice","Return order issued.") end
    if engine.input_pressed("follow") then Orders.follow("team","player") end
    if engine.input_pressed("regroup") then Orders.recall("team","player") end
    Orders.update("team")
    local arrived=0
    for _,id in ipairs(units) do
        if engine.exists(id) and engine.navigation_status(id).status == "arrived" then
            arrived+=1;engine.play_animation(id,"idle",false,.15)
            if engine.state_get("assignment:"..id,"")=="gather" and not engine.state_get("cargo:"..id,false) then
                engine.state_set("cargo:"..id,true);engine.set_opacity(id,.82);engine.set_visual_scale(id,1.1,1.1,1.1)
            elseif engine.state_get("assignment:"..id,"")=="return" and engine.state_get("cargo:"..id,false) then
                engine.state_set("cargo:"..id,false);Inventory.add("light",1);engine.set_opacity(id,1);engine.set_visual_scale(id,1,1,1)
            end
        end
    end
    engine.state_set("arrived",arrived)
    if Inventory.count("light")>=100 then Quests.complete("assembly");engine.message("100 lights delivered");engine.win() end
    refresh()
end}
'''
def strategy():
    root,p=common('strategy');p['title']='Mosslight Assembly';p['definitions']={'items':'data/items.json','quests':'data/quests.json'};p['prefabs']={'worker':'prefabs/worker.json'}
    p['navigation']={'cell_size':.25,'cell_height':.1,'agent_radius':.15,'agent_height':.8,'max_step':.3}
    p['camera']={'position':[0,25,24],'target':[0,0,0]}
    p['input']['actions'].update({'command':{'keys':['C']},'recall':{'keys':['V']},'spawn':{'keys':['N']},'dismiss':{'keys':['H']},'select_all':{'keys':['1']},'select_first':{'keys':['2']},'select_second':{'keys':['3']},'follow':{'keys':['B']},'regroup':{'keys':['J']}})
    p['entities']=[entity('ground',[0,-.5,0],[32,1,26],body='static',color=[.12,.2,.22,1]),
        entity('wall',[0,1,0],[2,2,11],body='static',color=[.35,.4,.48,1]),
        entity('player',[-12,1,6],[.7,1.8,.7],body='dynamic',shape='capsule',controller='character',role='player',asset='robot',animation='idle'),
        entity('home',[-7,.06,-.3],[8,.1,5],color=[.18,.4,.6,1]),entity('field',[7.6,.06,-.3],[8,.1,5],color=[.65,.48,.18,1])]
    write(root,'prefabs/worker.json',entity('worker',[0,.45,0],[.5,.85,.5],asset='robot',animation='idle',role='worker'))
    write(root,'data/items.json',{'light':{'name':'Mosslight','limit':100000}});write(root,'data/quests.json',{'assembly':{'title':'Deliver 100 lights'}})
    write(root,'scripts/game.luau',STRATEGY);write(root,'project.json',p)
    write(root,'verification-input.json',[{'from':5,'to':6,'actions':{'command':1}},{'from':1500,'to':1501,'actions':{'recall':1}},{'from':3000,'to':3001,'actions':{'spawn':1}},{'from':3002,'to':3003,'actions':{'dismiss':1}}])
    write(root,'README.md','''# Mosslight Assembly
An original strategy reference with 128 runtime-spawned, articulated workers,
navigation around a wall, staggered group orders, persistent cargo and UI.

Run `langmake play <this-directory>` or repository command
`build/langmake.exe play examples/strategy`. WASD moves the commander. C orders
gathering on the amber field; wait for arrivals, then V brings cargo to the blue
home field. N creates a worker (160 cap); H dismisses the last worker. F6 saves
slot `assembly`; F9 loads after a save. Deliver 100 lights to complete the task.
Keys 1/2/3 select all/first half/second half. C/V affect the selection; B follows
the commander continuously, J recalls once. Formation columns/spacing are editable
through the orders module. Current selection and orders survive saves.

The example stresses engine capacity and is deliberately a small creative slice.
Units have simple task states and capsule-free navigation motion; they do not
represent a complete RTS unit/combat framework. Procedural worker poses, cargo,
navigation status, group queues and scores are inspectable. CPU skinning has a
visible-vertex budget; keep mass-unit art simple and inspect timings.

Human feedback: Can you follow a group order visually? Is the obstacle route
clear? Which command or worker behavior would make the next iteration useful?
''')

SHARED2D=r'''
local Inventory,Quests,Dialogue=require("inventory"),require("quests"),require("dialogue")
local function refresh()
    engine.ui_set("title","Lantern Post")
    engine.ui_set("objective",engine.scene()=="garden" and "The garden lantern is lit. E at the blue door returns to the village." or "Talk to the keeper, collect the star, then use the post on the right.")
    engine.ui_set("inventory","Stars: "..Inventory.count("star"))
    engine.ui_set("health",Quests.get("post").completed and "Lantern lit" or "Lantern waiting")
    engine.ui_set("healthbar",{value=Quests.get("post").completed and 1 or 0,max=1})
    engine.ui_set("status",Quests.get("post").completed and "Lantern restored." or "Find a light for the old post.")
    engine.ui_set("controls","WASD move | E talk/use | 1/2 choices | F6 save | F9 load")
    Dialogue.refresh()
end
return {start=refresh,on_load=refresh,update=function(dt)
    if engine.input_pressed("save") then engine.save_game("lantern") end
    if engine.input_pressed("load") then engine.load_game("lantern") end
    if Dialogue.active() then engine.set_velocity("player",0,0,0);Dialogue.update();refresh();return end
    engine.set_velocity("player",engine.input("move_x")*3,engine.input("move_z")*3,0)
    if engine.exists("star") and engine.distance("player","star")<1 then Inventory.add("star",1);engine.destroy("star") end
    if engine.input_pressed("interact") then
        if engine.scene()=="garden" then
            if engine.distance("player","return")<2 then engine.change_scene("village") end
        elseif engine.distance("player","keeper")<2 then Dialogue.open("hello")
        elseif engine.distance("player","post")<2 then
            if Quests.get("post").completed then engine.change_scene("garden")
            elseif engine.state_get("accepted",false) and Inventory.add("star",-1) then
                Quests.complete("post");engine.set_color("post",1,.85,.25);engine.message("Lantern restored");engine.change_scene("garden")
            end
        end
    end
    if Quests.get("post").completed then engine.win() end
    refresh()
end}
'''
def shared2d():
    root,p=common('shared-2d','2d');p['title']='Lantern Post';p['definitions']={'items':'data/items.json','quests':'data/quests.json','dialogue':'data/dialogue.json'}
    p['assets'].pop('robot');p['assets']['walker']={'type':'sprite','path':'assets/walker.sprite.json'}
    write(root,'assets/walker.sprite.json',{'schema_version':1,'width':8,'height':8,'palette':{'.':'#00000000','G':'#A1D99C','D':'#244251'},'frames':[{'id':'stand','pixels':['..GGGG..','.GGGGGG.','.GDDDDG.','..GGGG..','..GGGG..','.GGGGGG.','..G..G..','.GG..GG.']}]})
    p['entry_scene']='village';p['scenes']={'village':'scenes/village.json','garden':'scenes/garden.json'}
    p['entities']=[]
    village=[entity('player',[-5,0,0],[1,1,1],body='dynamic',role='player',asset='walker'),entity('keeper',[-3,1,0],[1,1,1],asset='walker'),entity('star',[0,0,0],[.5,.5,.5],color=[1,.8,.2,1]),entity('post',[5,0,0],[1,2,1],color=[.3,.5,.6,1])]
    garden=[entity('player',[0,0,0],[1,1,1],body='dynamic',role='player',asset='walker'),entity('return',[-1,0,0],[1,2,1],color=[.3,.5,.8,1]),entity('lantern',[3,0,0],[1,2,1],color=[1,.85,.25,1]),entity('hedge',[1,3,0],[10,1,1],body='static',color=[.2,.5,.3,1])]
    write(root,'scenes/village.json',{'entities':village});write(root,'scenes/garden.json',{'entities':garden})
    write(root,'data/items.json',{'star':{'name':'Light star','limit':9}});write(root,'data/quests.json',{'post':{'title':'Restore the lantern'}})
    write(root,'data/dialogue.json',{'hello':{'speaker':'Post keeper','text':'Will you carry a star to the lantern?','choices':[{'text':'1 / E: Yes.','flag':'accepted','value':True},{'text':'2: Not yet.'}]}})
    write(root,'scripts/game.luau',SHARED2D);write(root,'project.json',p)
    write(root,'verification-input.json',[{'from':0,'to':30,'move_x':1},{'from':31,'to':32,'actions':{'interact':1}},{'from':33,'to':34,'actions':{'choice1':1}},{'from':35,'to':175,'move_x':1},{'from':176,'to':177,'actions':{'interact':1}}])
    write(root,'README.md','''# Lantern Post
Run `langmake play <this-directory>` or `build/langmake.exe play examples/shared-2d`.
WASD moves, E talks/uses, 1/2 selects dialogue, F6 saves `lantern`, F9 loads after a
save. Talk to the keeper, collect the star, use the tall post on the right.
Lighting the post opens a second garden area. Use E at its blue door to return;
the village remembers the collected star and restored post. Both areas and their
progress survive player save/load.

This 2D reference reuses the same inventory, quest, dialogue, save and anchored UI
modules as the 3D adventure. Art is an editable eight-pixel sprite recipe. The
recorded input timeline checks technical behavior; the human should evaluate
readability, interaction feel and the next creative iteration.
''')

def verify(engine, graphics=False):
    evidence=ROOT/'.cache'/'gameplay-reference-verification'
    evidence.mkdir(parents=True,exist_ok=True)
    checks=[]
    def run(*args):
        command=[str(engine),*map(str,args)]
        result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=120)
        try: output=json.loads(result.stdout)
        except ValueError: raise AssertionError('Non-JSON command result: '+result.stdout[-500:]+result.stderr[-500:])
        if result.returncode or not output.get('ok'):raise AssertionError(output)
        return output
    def check(condition, message):
        if not condition:raise AssertionError(message)
        checks.append(message)
    def observable(state):
        keys=['state','tick','entities','won','scene','ui','camera','navigation_agents','asset_revisions']
        return {key:state.get(key) for key in keys}
    profiles={}
    module_project=evidence/'module-contracts'
    source=ROOT/'examples'/'adventure'
    module_manifest={'schema_version':2,'dimension':'3d','title':'Module contract checks','script':'scripts/test.luau',
        'entities':[entity('signal_orb',[2,2,0],[.35,.35,.35],shape='sphere')],
        'modules':{name:'modules/'+name+'.luau' for name in ('inventory','dialogue','cinematics','orders','fsm')},
        'definitions':{name:'data/'+name+'.json' for name in ('items','dialogue','cinematics')}}
    for relative in [*module_manifest['modules'].values(),*module_manifest['definitions'].values()]:
        write(module_project,relative,(source/relative).read_text())
    write(module_project,'project.json',module_manifest)
    write(module_project,'scripts/test.luau',r'''
local I,D,C=require("inventory"),require("dialogue"),require("cinematics")
local O,F=require("orders"),require("fsm")
return {start=function()
    D.open("hello"); D.choose(2); D.choose(1); D.choose(1)
    assert(engine.state_get("accepted") and I.count("tuner")==1,"history branch reward/flag")
    D.open("hello"); D.choose(1); D.choose(1)
    assert(I.count("tuner")==1,"repeated dialogue must not duplicate reward")
    assert(not I.equip("head","tuner") and I.equip("hand","tuner"),"equipment slot validation")
    assert(I.equipped("hand")=="tuner" and not I.add("tuner",-1),"equipped item retained")
    assert(I.unequip("hand")=="tuner" and I.add("tuner",-1),"unequip and remove")
    assert(not I.equip("hand","tuner") and I.add("tuner",1) and I.equip("hand","tuner"),"cannot equip absent item")
    local granted,reason=I.grant("too-many",{coil=10})
    assert(not granted and reason=="capacity" and I.count("coil")==0,"over-capacity reward is atomic")
    assert(I.grant("valid",{coil=2}) and I.count("coil")==2,"valid reward grant")
    O.select("fixture",{"signal_orb","signal_orb","missing"})
    assert(#O.selected("fixture")==1,"selection filters duplicates/dead entities")
    O.formation("fixture",2,.75); O.assign("fixture",1,2,3,"repair")
    assert(engine.state_get("order:fixture").task=="repair" and O.formation("fixture").columns==2,"formation/task assignment")
    O.follow("fixture","signal_orb"); assert(engine.state_get("follow:fixture").leader=="signal_orb","follow leader persisted")
    O.recall("fixture","signal_orb"); assert(engine.state_get("follow:fixture")==nil and engine.state_get("order:fixture").task=="recall","recall cancels follow")
    F.after("instant",0); assert(F.due("instant") and not F.due("instant"),"timer fires once")
    F.after("later",3)
    engine.state_set("module_contracts",true)
    C.play("restored")
end,update=function() if F.due("later") then engine.state_set("timer_fired",engine.tick()) end; C.update() end}
''')
    module_result=run('simulate','--project',module_project,'--steps',200)
    check(module_result['state']['state']['module_contracts'],'branching dialogue rewards, slot equipment and bounded inventory contracts')
    check(module_result['state']['state']['timer_fired']==3,'persistent timers, selection, formation, follow and recall contracts')
    orb=next(e for e in module_result['state']['entities'] if e['id']=='signal_orb')
    check(orb['position']==[2.0,4.0,0.0] and orb['rotation']==[0.0,180.0,0.0],'cinematic actor tracks reach exact position and rotation')
    for name,steps in [('adventure',600),('strategy',3300),('shared-2d',300)]:
        project=ROOT/'examples'/name
        timeline=project/'verification-input.json'
        run('validate','--project',project)
        run('check','--project',project)
        final=run('simulate','--project',project,'--steps',steps,'--input',timeline,'--output',evidence/(name+'.json'))
        check(final['state']['won'],name+' completes its technical scenario')
        repeated=run('simulate','--project',project,'--steps',steps,'--input',timeline)
        check(observable(final['state'])==observable(repeated['state']),name+' repeats the same state on this host')
        profiles[name]=final['profile']
        if name=='adventure':
            check(final['state']['scene']=='relay' and final['state']['state']['quests']['signal']['completed'],'adventure changes area and retains completed quest')
            check(final['state']['state']['inventory']['coil']==0 and final['state']['state']['sentinel_down'],'adventure consumes item and persists combat outcome')
            check(final['state']['state']['equipment']['hand']=='tuner' and final['state']['state']['inventory']['tuner']==1,'dialogue grants and equips the combat tool exactly once')
            save=evidence/'adventure-save.json'
            mid=run('simulate','--project',project,'--steps',350,'--input',timeline,'--save-output',save)
            restored=run('simulate','--project',project,'--steps',0,'--load-save',save)
            check(restored['state']['state']==mid['state']['state'],'save restores declared gameplay state without duplicate rewards')
            shifted=[]
            for event in json.loads(timeline.read_text())['events']:
                if event['to']>350:
                    shifted.append(dict(event,**{'from':max(0,event['from']-350),'to':event['to']-350}))
            suffix=evidence/'adventure-resume-input.json';suffix.write_text(json.dumps({'events':shifted}))
            resumed=run('simulate','--project',project,'--steps',250,'--input',suffix,'--load-save',save)
            check(resumed['state']['won'] and resumed['state']['state']['quests']['signal']['completed'],'saved adventure resumes to completion')
            mid_cinematic=evidence/'adventure-cinematic-save.json'
            run('simulate','--project',project,'--steps',430,'--input',timeline,'--save-output',mid_cinematic)
            resumed_cinematic=run('simulate','--project',project,'--steps',170,'--load-save',mid_cinematic)
            orb=next(e for e in resumed_cinematic['state']['entities'] if e['id']=='signal_orb')
            check(orb['position']==[2.0,4.0,0.0] and resumed_cinematic['state']['state']['cinematic']['completed'],'cinematic actor tracks resume after save without restarting')
        if name=='strategy':
            check(len(final['state']['state']['workers'])==128 and 100<=final['state']['state']['inventory']['light']<=128,'128 workers navigate and deliver the objective with spawn/despawn preserved')
            group_input=evidence/'strategy-selection-input.json'
            write(evidence,'strategy-selection-input.json',{'events':[{'from':0,'to':1,'actions':{'select_first':1}},{'from':2,'to':3,'actions':{'follow':1}}]})
            group_save=evidence/'strategy-selection-save.json'
            selected=run('simulate','--project',project,'--steps',5,'--input',group_input,'--save-output',group_save)
            selected_state=selected['state']['state']
            check(len(selected_state['selection:team'])==64 and selected_state['follow:team']['leader']=='player' and len(selected['state']['navigation_agents'])>0,'selected half-team follows commander through native navigation')
            restored_group=run('simulate','--project',project,'--steps',0,'--load-save',group_save)
            check(restored_group['state']['state']['selection:team']==selected_state['selection:team'] and restored_group['state']['state']['follow:team']==selected_state['follow:team'],'selection and continuous follow survive save/load')
        if name=='shared-2d':
            check(final['state']['scene']=='garden' and final['state']['state']['inventory']['star']==0,'2D scene transition carries completed quest and consumed item')
            save=evidence/'shared-2d-save.json'
            run('simulate','--project',project,'--steps',300,'--input',timeline,'--save-output',save)
            return_input=evidence/'shared-2d-return-input.json'
            write(evidence,'shared-2d-return-input.json',{'events':[{'from':0,'to':1,'actions':{'interact':1}}]})
            returned=run('simulate','--project',project,'--steps',2,'--load-save',save,'--input',return_input)
            check(returned['state']['scene']=='village' and not any(e['id']=='star' for e in returned['state']['entities']) and returned['state']['state']['quests']['post']['completed'],'2D save/load returns to persistent Box2D area without duplicate collectible')
        if graphics:
            ticks=[0,steps//2,steps]
            directory=evidence/name
            run('capture','--project',project,'--steps',steps,'--input',timeline,'--ticks',','.join(map(str,ticks)),'--output-dir',directory,'--no-overlay','--width',960,'--height',600)
            for tick in ticks:
                side=json.loads((directory/('tick-'+str(tick)+'.png.json')).read_text())
                at_tick=run('simulate','--project',project,'--steps',tick,'--input',timeline)['state']
                check(observable(side)==observable(at_tick),name+' capture matches simulation tick '+str(tick))
                check(side['source_revision']==at_tick['source_revision'] and not side['overlay'],name+' capture records source and overlay at tick '+str(tick))
    report={'ok':True,'checks':checks,'profiles':profiles,'graphics':graphics,'human_playtesting':'awaiting human feedback'}
    (evidence/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'ok':True,'checks':len(checks),'graphics':graphics,'report':str(evidence/'report.json')}))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify',action='store_true',help='Test existing references without regenerating them')
    parser.add_argument('--graphics',action='store_true',help='Also run isolated exact-tick graphical evidence')
    parser.add_argument('--engine',type=Path,default=ROOT/'build'/'langmake.exe')
    args=parser.parse_args()
    if args.verify:verify(args.engine.resolve(),args.graphics)
    else:
        adventure();strategy();shared2d()
        print('Generated adventure, strategy and shared-2d reference projects.')
