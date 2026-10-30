"""Alternate native and Pyodide per item, one fresh process each, so CPU clock
drift hits both sides alike. Appends JSON lines to results.jsonl."""
import json
import os
import subprocess
import sys
import time

BENCH = os.path.dirname(os.path.abspath(__file__))
SCRATCH = os.path.dirname(BENCH)
REPO = r"C:\Users\admin\Documents\AnimeAn"
PY = os.path.join(REPO, "dist", "AnimeAn", "python312", "python.exe")
PYODIDE = os.path.join(SCRATCH, "pyodide_dl", "node_modules", "pyodide")

SHORT = ["__ops__", "t_additional", "t_axis_crossing", "t_bridge", "t_chord_intersect",
         "t_compose", "t_flowfield", "t_horizon3d", "t_removal", "t_sever",
         "t_third_invalidate"]
ROUNDS = {name: 3 for name in SHORT}
ROUNDS["t_bezier_knots"] = 1


def run(rig, name):
    if rig == "native":
        cmd = [PY, os.path.join(BENCH, "run_one.py"), REPO, name]
    else:
        cmd = ["node", os.path.join(BENCH, "run_one_pyodide.mjs"), PYODIDE, REPO, BENCH, name]
    start = time.perf_counter()
    proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf8",
                          errors="replace", timeout=1500)
    wall = time.perf_counter() - start
    line = next((l for l in proc.stdout.splitlines() if l.startswith("RESULT ")), None)
    if line is None:
        return {"rig": rig, "name": name, "wall": wall, "error": (proc.stderr or proc.stdout)[-900:]}
    data = json.loads(line[7:])
    data.update(rig=rig, wall=wall)
    return data


with open(os.path.join(BENCH, "results.jsonl"), "a", encoding="utf8") as sink:
    for round_index in range(3):
        for name, rounds in ROUNDS.items():
            if round_index >= rounds:
                continue
            order = ("native", "pyodide") if round_index % 2 == 0 else ("pyodide", "native")
            for rig in order:
                data = run(rig, name)
                data["round"] = round_index
                sink.write(json.dumps(data) + "\n")
                sink.flush()
                print(json.dumps(data)[:240], flush=True)
print("DONE", flush=True)
