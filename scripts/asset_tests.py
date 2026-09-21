"""End-to-end text asset, gameplay, and live GPU checks; Python standard library only."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time
import wave
import zlib
from integration_tests import ROOT, Suite, require, write_json, validate_png


def read_pixels(path):
    """Decode non-interlaced LodePNG output to RGB for actual rendered-pixel checks."""
    data = path.read_bytes()
    offset, packed, palette = 8, bytearray(), []
    while offset < len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset+4:offset+8]
        value = data[offset+8:offset+8+length]
        if kind == b"IHDR":
            width,height,bits,mode,_,_,interlace = struct.unpack(">IIBBBBB", value)
            require(interlace == 0, "pixel test expects noninterlaced PNG")
        elif kind == b"PLTE": palette = [tuple(value[i:i+3]) for i in range(0,len(value),3)]
        elif kind == b"IDAT": packed.extend(value)
        offset += length + 12
    channels = {0:1,2:3,3:1,4:2,6:4}[mode]
    stride = (width*channels*bits+7)//8
    bpp = max(1,(channels*bits+7)//8)
    raw = zlib.decompress(packed)
    previous = bytearray(stride)
    pixels = []
    for y in range(height):
        method = raw[y*(stride+1)]
        row = bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
        for x in range(stride):
            a = row[x-bpp] if x >= bpp else 0
            b = previous[x]
            c = previous[x-bpp] if x >= bpp else 0
            p = a+b-c
            pa,pb,pc = abs(p-a),abs(p-b),abs(p-c)
            prediction = a if pa <= pb and pa <= pc else b if pb <= pc else c
            row[x] = (row[x] + (0,a,b,(a+b)//2,prediction)[method]) & 255
        for x in range(width):
            if mode == 3:
                index = (row[x*bits//8] >> (8-bits-(x*bits%8))) & ((1<<bits)-1)
                pixels.append(palette[index])
            else:
                require(bits == 8, "RGB/grayscale test expects 8-bit PNG")
                value = row[x*channels:(x+1)*channels]
                pixels.append(tuple(value[:3]) if mode in (2,6) else (value[0],)*3)
        previous = row
    return width,height,pixels


class Assets(Suite):
    def sources_and_exports(self):
        for kind in ("sprite","mesh","music"):
            schema = self.run("asset","schema","--type",kind)
            require(schema["type"] == "object", "recipe schema discovery")
        self.passed("all three recipe schemas are discoverable")
        for dimension in ("2d","3d"):
            project = self.directory / ("rockets-"+dimension)
            self.run("new","--template","rockets-"+dimension,"--output",project)
            original = ROOT / "examples" / ("rockets-"+dimension)
            for source in (original/"assets").iterdir():
                require((project/"assets"/source.name).read_bytes() == source.read_bytes(),"new preserves every asset source")
            self.run("validate","--project",project)
            listing = self.run("asset","list","--project",project)
            require(len(listing["assets"]) == 6,"all six demo assets compile")
            game = self.run("simulate","--project",project,"--steps",480,"--input",original/"tests/win.json")
            require(game["state"]["won"] and game["state"]["collected"] == 5,"rocket game can win")
            require(len(game["state"]["audio_events"]) > 5,"laser game actions emit audio events")
            require(game["state"]["music"] == "theme","background score is observable")
            self.passed(dimension+" starter copies source graph and wins with deterministic input")
            if self.graphics:
                image = self.directory / (dimension+"-game.png")
                self.run("capture","--project",project,"--frames",50,"--output",image,timeout=60)
                validate_png(image,1280,800)
                meta = json.loads(Path(str(image)+".json").read_text())
                require(meta["tick"] == 50 and len(meta["asset_revisions"]) == 6,"capture carries asset revisions and exact tick")
                width,height,pixels = read_pixels(image)
                require(len(set(pixels[200*width:700*width])) > 12,"game capture contains rendered colored assets")
                self.passed(dimension+" asset game renders with correlated evidence")

        project = self.directory/"rockets-2d"
        sheet = self.directory/"rocket.png"
        info = self.run("asset","render","--project",project,"--id","rocket","--scale",4,"--output",sheet)
        validate_png(sheet,24*3*4,40*4)
        require(info["default_animation"] == "fly","sprite inspection exposes animation")
        output = self.directory/"alien.obj"
        info = self.run("asset","export","--project",self.directory/"rockets-3d","--id","alien","--output",output)
        lines = output.read_text().splitlines()
        require(sum(x.startswith("f ") for x in lines) == info["triangles"],"OBJ triangles match compiled mesh")
        require(sum(x.startswith("g ") for x in lines) == len(info["parts"]),"OBJ preserves named parts")
        wav = self.directory/"theme.wav"
        result = self.run("asset","render","--project",project,"--id","theme","--output",wav)
        with wave.open(str(wav),"rb") as sound:
            require(sound.getnchannels()==2 and sound.getframerate()==48000 and sound.getsampwidth()==2,"WAV is stereo PCM16/48k")
            require(sound.getnframes()==result["audio"]["sample_frames"],"WAV frames match report")
        require(result["audio"]["peak"] > .1 and result["audio"]["clipped_frames"] == 0,"demo audio contains unclipped signal")
        second = self.directory/"theme-again.wav"
        self.run("asset","render","--project",project,"--id","theme","--output",second)
        require(wav.read_bytes()==second.read_bytes(),"music synthesis is repeatable")
        self.passed("PNG frame sheet, named OBJ parts, and deterministic unclipped stereo WAV exports")
        built = self.run("asset","build","--project",project)
        require(len(built["assets"])==6 and all(Path(x["output"]).is_file() for x in built["assets"]),"asset build writes all derived outputs")
        self.passed("revision-named derived asset build")

        recipe = project/"assets/rocket.sprite.json"
        before = recipe.read_bytes()
        revision = self.run("inspect","--project",project)["revision"]
        self.run("checkpoint","--project",project,"--name","before-art")
        changed = json.loads(before)
        changed["palette"]["R"] = "#00FFFFFF"
        proposal = self.directory/"proposal.json"
        write_json(proposal,changed)
        reply = self.run("asset","replace","--project",project,"--id","rocket","--file",proposal,"--expected-revision",revision)
        require(reply["ok"] and recipe.read_bytes()!=before,"valid asset replacement persists")
        self.run("asset","replace","--project",project,"--id","rocket","--file",proposal,"--expected-revision",revision,ok=False)
        current = self.run("inspect","--project",project)["revision"]
        changed["width"] = 0
        write_json(proposal,changed)
        rejected = recipe.read_bytes()
        self.run("asset","replace","--project",project,"--id","rocket","--file",proposal,"--expected-revision",current,ok=False)
        require(recipe.read_bytes()==rejected,"invalid recipe leaves live source intact")
        self.run("asset","export","--project",project,"--id","rocket","--output",recipe,ok=False)
        require(recipe.read_bytes()==rejected,"export protects authored sources")
        self.run("restore","--project",project,"--name","before-art")
        require(recipe.read_bytes()==before and self.run("inspect","--project",project)["revision"]==revision,"checkpoint restores original asset bytes/revision")
        self.passed("asset replacement, stale revision rejection, invalid edit recovery and source protection")
        protected=self.directory/"protected"
        self.run("new","--template","rockets-2d","--output",protected)
        atlas=protected/"assets/source.png"
        atlas.write_bytes(sheet.read_bytes())
        document=json.loads((protected/"project.json").read_text())
        document["assets"]["rocket"]["path"]="assets/guard.png.json"
        write_json(protected/"project.json",document)
        recipe_doc=json.loads(before)
        recipe_doc["source_png"]="assets/source.png"
        recipe_doc["frames"][0]["source_rect"]=[0,0,24,40]
        guard=protected/"assets/guard.png.json"
        write_json(guard,recipe_doc)
        self.run("validate","--project",protected)
        atlas_bytes=atlas.read_bytes(); guard_bytes=guard.read_bytes()
        for command in ("asset-export","capture","simulate"):
            prefix=["asset","export","--id","rocket"] if command=="asset-export" else [command]
            result=self.run(*prefix,"--project",protected,"--output",atlas,ok=False)
            require("authored source" in str(result),"same-extension source protection must trigger before output write")
        result=self.run("capture","--project",protected,"--output",protected/"assets/guard.png",ok=False)
        require("authored source" in str(result),"capture sidecar cannot replace recipe")
        result=self.run("asset","render","--project",protected,"--id","rocket","--output",protected/"assets/guard.png",ok=False)
        require("authored source" in str(result),"asset sidecar cannot replace recipe")
        require(atlas.read_bytes()==atlas_bytes and guard.read_bytes()==guard_bytes,"all authored bytes remain intact")
        duplicate=self.directory/"duplicate.json"
        duplicate.write_text('{"schema_version":1,"schema_version":1}')
        rev=self.run("inspect","--project",protected)["revision"]
        result=self.run("asset","replace","--project",protected,"--id","rocket","--file",duplicate,"--expected-revision",rev,ok=False)
        require("Duplicate JSON" in str(result),"asset proposal duplicate keys rejected before mutation")
        require(guard.read_bytes()==guard_bytes,"duplicate proposal does not mutate source")
        self.passed("same-extension PNG input and sidecar protection; strict duplicate-key proposals")
        if self.graphics:
            image = self.directory/"rocket-3d.png"
            self.run("asset","render","--project",self.directory/"rockets-3d","--id","rocket","--output",image,timeout=60)
            validate_png(image,1280,800)
            meta = json.loads(Path(str(image)+".json").read_text())
            require(meta["tick"]==0 and meta["asset"]["id"]=="rocket","isolated mesh preview keeps source identity")
            self.passed("isolated procedural mesh PNG preview")

    def live_assets(self):
        project = self.directory/"rockets-2d"
        recipe = project/"assets/rocket.sprite.json"
        original = recipe.read_bytes()
        process = None
        with (self.directory/"live.stdout").open("w") as out, (self.directory/"live.stderr").open("w") as err:
            try:
                process = subprocess.Popen([str(self.engine),"preview","--project",str(project)],cwd=self.directory,stdout=out,stderr=err)
                def control(action,*arguments): return self.run("control","--project",project,"--action",action,"--include-state",*arguments)
                def until(predicate):
                    deadline=time.monotonic()+10
                    while time.monotonic()<deadline:
                        target=project/".langmake/live.json"
                        if target.exists():
                            # Use the replacement-compatible native reader;
                            # Python's ordinary Windows open can race a rename.
                            state=self.run("inspect","--project",project,"--live","--limit",0)
                            if state.get("fresh") and state.get("running") and predicate(state): return state
                        require(process.poll() is None,"live asset preview remains running")
                        time.sleep(.1)
                    raise AssertionError("asset reload did not reach expected state")
                state=until(lambda s:"asset_revisions" in s)
                control("pause")
                control("reset")
                image=Path(control("capture")["image"])
                w,h,old_pixels=read_pixels(image)
                changed=json.loads(original)
                changed["palette"]["R"]="#00FFFFFF"
                write_json(recipe,changed)
                new=until(lambda s:s["source_revision"]!=state["source_revision"] and not s.get("diagnostic"))
                require(new["asset_revisions"]["rocket"]!=state["asset_revisions"]["rocket"],"live recipe change rebuilds its sprite")
                image=Path(control("capture")["image"])
                _,_,new_pixels=read_pixels(image)
                # Rocket is bottom-center; ignore all status text when comparing.
                differences=sum(old_pixels[y*w+x]!=new_pixels[y*w+x] for y in range(555,690) for x in range(595,685))
                require(differences>20,"palette edit actually changes GPU-rendered rocket pixels")
                good_revision=new["source_revision"]
                changed["width"]=0
                write_json(recipe,changed)
                bad=until(lambda s:bool(s.get("diagnostic")))
                require(bad["source_revision"]==good_revision,"invalid recipe preserves last good running world")
                recipe.write_bytes(original)
                until(lambda s:s["source_revision"]==state["source_revision"] and not s.get("diagnostic"))
                input_file=self.directory/"fire.json"
                write_json(input_file,{"fire":True})
                stepped=control("step","--steps",50,"--input",input_file)["state"]
                require(stepped["audio_events"] and stepped["collected"]>=1,"live firing emits sound events and defeats center alien")
                self.passed("live audio initialization, actual sprite pixel reload, invalid-recipe retention, recovery and scripted firing")
                self.audio_available=state.get("audio_available")
            finally:
                if process and process.poll() is None:
                    process.terminate(); process.wait(timeout=10)
                recipe.write_bytes(original)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine",required=True,type=Path)
    parser.add_argument("--graphics",action="store_true")
    args=parser.parse_args()
    suite=Assets(args.engine,args.graphics)
    print("Asset fixtures: "+str(suite.directory),flush=True)
    try:
        suite.sources_and_exports()
        if args.graphics: suite.live_assets()
    except Exception as error:
        print("FAIL "+str(error)+"\nReport: "+str(suite.report(error)),flush=True)
        return 1
    report=suite.report()
    value=json.loads(report.read_text()); value["audio_available"]=getattr(suite,"audio_available",None)
    write_json(report,value)
    print("All {} asset integration checks passed. Report: {}".format(len(suite.completed),report),flush=True)
    return 0

if __name__=="__main__": raise SystemExit(main())
