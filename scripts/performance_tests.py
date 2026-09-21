"""Measure the 128-agent reference workload and process memory on Windows.

All fixtures and reports stay in .cache/performance-tests. This is technical
qualification, not gameplay acceptance. Requires only Python's standard library.
"""
import argparse
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import shutil
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]


class Memory(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("faults", wintypes.DWORD)] + [(x, ctypes.c_size_t) for x in
        ("peak_working", "working", "peak_paged", "paged", "peak_nonpaged", "nonpaged", "pagefile", "peak_pagefile", "private")]


def hardware():
    command = "@{cpu=Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors; gpu=Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion; os=Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,TotalVisibleMemorySize} | ConvertTo-Json -Depth 4"
    return json.loads(subprocess.check_output(["powershell", "-NoProfile", "-Command", command], text=True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--steps", type=int, default=36000)
    parser.add_argument("--graphics", action="store_true")
    args = parser.parse_args()
    engine = args.engine.resolve()
    work = ROOT / ".cache/performance-tests" / uuid.uuid4().hex
    project = work / "strategy"
    shutil.copytree(ROOT / "examples/strategy", project, ignore=shutil.ignore_patterns(".langmake"))
    # Keep all agents moving and re-pathing throughout ten simulated minutes.
    game = project / "scripts/game.luau"
    original = game.read_text(encoding="utf-8")
    game.write_text("local game = (function()\n" + original + "\nend)()\n" + '''
return {start=game.start,on_load=game.on_load,update=function(dt)
    game.update(dt)
    local tick=engine.tick()
    if tick%600==0 then
        local side=math.floor(tick/600)%2==0 and 1 or -1
        for i,id in ipairs(engine.state_get("workers",{})) do
            engine.navigate(id,side*8+((i-1)%16)*.25,.1,-2+math.floor((i-1)/16)*.5,5)
        end
    end
end}
''', encoding="utf-8")
    result_path = work / "simulation.json"
    report = {"hardware": hardware(), "engine": str(engine), "steps": args.steps, "memory_samples": []}
    with (work / "stdout.json").open("w", encoding="utf-8") as out, (work / "stderr.txt").open("w", encoding="utf-8") as err:
        process = subprocess.Popen([str(engine), "profile", "--project", str(project), "--steps", str(args.steps), "--output", str(result_path)], stdout=out, stderr=err)
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = kernel.OpenProcess(0x410, False, process.pid)
        psapi = ctypes.WinDLL("psapi", use_last_error=True)
        psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(Memory), wintypes.DWORD]
        began = time.monotonic()
        try:
            while process.poll() is None:
                memory = Memory(); memory.cb = ctypes.sizeof(memory)
                if psapi.GetProcessMemoryInfo(handle, ctypes.byref(memory), memory.cb):
                    report["memory_samples"].append({"seconds": round(time.monotonic()-began, 3), "working_set_bytes": memory.working, "private_bytes": memory.private})
                if time.monotonic()-began > 300:
                    process.kill(); raise AssertionError("Reference simulation timed out")
                time.sleep(.5)
        finally:
            kernel.CloseHandle(handle)
        assert process.returncode == 0, (work / "stdout.json").read_text(encoding="utf-8")
    result = json.loads(result_path.read_text())
    report["simulation_profile"] = result["profile"]
    report["wall_seconds"] = time.monotonic()-began
    steady = report["memory_samples"][max(2, len(report["memory_samples"])//4):]
    assert steady, "Not enough memory samples"
    report["steady_private_growth_bytes"] = steady[-1]["private_bytes"]-steady[0]["private_bytes"]
    report["steady_private_range_bytes"] = max(s["private_bytes"] for s in steady)-min(s["private_bytes"] for s in steady)
    assert result["state"]["statistics"]["navigation_agents"] == 128
    assert result["profile"]["step_p95_ms"] < 1000/60, "128-agent CPU workload exceeds 60 Hz p95 budget"
    assert report["steady_private_growth_bytes"] < 16*1024*1024, "Sustained private memory grows over 16 MiB"
    if args.graphics:
        capture = subprocess.run([str(engine), "capture", "--project", str(project), "--steps", "600", "--ticks", "0,600", "--output-dir", str(work/"captures"), "--no-overlay"], capture_output=True, text=True, timeout=90)
        assert capture.returncode == 0, capture.stdout+capture.stderr
        sidecar = json.loads((work/"captures/tick-600.png.json").read_text())
        report["graphics_profile"] = sidecar["profile"]
        report["graphics_viewport"] = [1280,800]
    report["ok"] = True
    (work/"report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"ok":True,"report":str(work/"report.json"),"profile":report["simulation_profile"],"private_growth_bytes":report["steady_private_growth_bytes"]},indent=2))


if __name__ == "__main__":
    main()
