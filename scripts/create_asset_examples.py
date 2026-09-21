"""Maintain the text-only rocket demonstration scenes; no art tools required."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = '''local t = 0
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
'''

def create(dimension):
    root = ROOT / "examples" / ("rockets-" + dimension)
    (root / "scripts").mkdir(parents=True, exist_ok=True)
    kind = "sprite" if dimension == "2d" else "mesh"
    assets = {name: {"type": kind, "path": f"assets/{name}.{kind}.json"}
              for name in ("rocket", "alien", "star", "planet")}
    assets.update({"theme": {"type": "music", "path": "assets/theme.music.json"},
                   "laser": {"type": "music", "path": "assets/laser.sfx.json"}})
    entities = []
    def add(id, asset, position, size, **extra):
        value = dict(id=id, name=id.replace("_", " ").title(), body="none",
                     role="decoration", position=position, size=size, asset=asset)
        value.update(extra)
        if not asset:
            del value["asset"]
        entities.append(value)
    add("rocket", "rocket", [0,-6,0], [1.8,3,1.8], role="player", body="dynamic")
    for i,x in enumerate([-8,-4,0,4,8],1):
        add(f"alien_{i}", "alien", [x,4.4,0], [2.2,2.2,1.6], role="collectible")
    for i in range(1,7):
        add(f"laser_{i}", "", [0,-20,0], [.16,.9,.16], visible=False, color=[.2,.95,1,1])
    add("distant_planet", "planet", [-11,1,-5], [5,5,5])
    for i in range(28):
        x = ((i*137+51)%260)/10-13
        y = ((i*83+33)%170)/10-8.5
        size = .11 + (i%3)*.07
        add(f"star_{i:02d}", "star", [x,y,-4], [size,size,size])
    data = dict(schema_version=1,dimension=dimension,title="Star Patrol / Text-authored " + dimension.upper(),
                gravity=[0,0,0],camera={"position":[4,5,31] if dimension=="3d" else [0,0,30],"target":[0,0,0]},
                script="scripts/game.luau",assets=assets,music="theme",entities=entities)
    (root/"project.json").write_text(json.dumps(data,indent=2)+"\n", encoding="utf-8")
    (root/"scripts/game.luau").write_text(SCRIPT,encoding="utf-8")
    # Stand still and fire, then steer across every remaining column while firing.
    (root/"tests").mkdir(exist_ok=True)
    events = [{"from":0,"to":80,"fire":True},
              {"from":80,"to":149,"move_x":-1,"fire":True},
              {"from":149,"to":229,"fire":True},
              {"from":229,"to":366,"move_x":1,"fire":True},
              {"from":366,"to":480,"fire":True}]
    (root/"tests/win.json").write_text(json.dumps({"events":events},indent=2)+"\n",encoding="utf-8")

if __name__ == "__main__":
    create("2d")
    create("3d")
