"""Guide edits must retire additional-line thirds - incl. delete-then-redraw."""
import math
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT + r"\pyfile")
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am

H = [(-300.0, 0.0), (300.0, 0.0)]
H2 = [(-300.0, 30.0), (300.0, 30.0)]
V = [(0.0, -200.0), (0.0, 200.0)]
BOW = [(x, 40.0 + 0.004 * (100.0 - x) * (100.0 + x)) for x in
       [-100.0 + 8.0 * k for k in range(26)]]
# Inside the pink band the mapping deliberately pins to the drawn main
# canvas (the board's authority), so redraw-change is probed OUTSIDE the
# band where the pure frame shift must show; scenario equality is probed
# ON the line where staleness once froze the deformation.
PROBE = (250.0, -150.0)
PROBE_ON = (0.0, 40.0)


def fresh_setup():
    am._MAPPING_ASSETS.clear()
    for view in ("child", "main"):
        am._MAPPING_ASSETS[view] = {
            am.H_PROPERTY: {"points": list(H), "width": 3.0},
            am.V_PROPERTY: {"points": list(V), "width": 3.0},
        }
    assert am.run_additional_line_tool("main", list(BOW))


def lines(view):
    return am._MAPPING_ASSETS[view][am.ADDITIONAL_PROPERTY]["lines"]


def image():
    mp, why = am._mapper_from_assets()
    assert mp is not None, why
    far = mp(PROBE)
    near = mp(PROBE_ON)
    return (far[0], far[1], near[0], near[1])


# 1) direct redraw of the main H guide retires the main-board thirds
fresh_setup()
before = image()
assert len(lines("main")[0].get("third") or []) == len(BOW)
assert am.run_center_line_tool("main", am.H_PROPERTY, list(H2))
assert not lines("main")[0].get("third"), "main third must be dropped"
assert lines("child")[0].get("third"), "child frame untouched - third kept"
direct = image()
assert math.hypot(direct[0] - before[0], direct[1] - before[1]) > 1.0
print("1) direct redraw: main third retired, mapping recomputed")

# 2) THE REPORTED BUG - delete the guide first, then draw the new one.
#    The redraw entry used to see no predecessor and skip the
#    invalidation, so the stale thirds kept describing the retired frame
#    and the deformation never updated.
fresh_setup()
am._overlay_removed(None, None,
                    {"overlay": {"id": am.H_PROPERTY}, "view": "main"})
assert am.H_PROPERTY not in am._MAPPING_ASSETS["main"]
assert not lines("main")[0].get("third"), \
    "deleting a guide must retire the thirds that lived in its frame"
assert am.run_center_line_tool("main", am.H_PROPERTY, list(H2))
redrawn = image()
assert max(abs(a - b) for a, b in zip(redrawn, direct)) < 1e-6, \
    (redrawn, direct)   # delete-then-redraw == direct redraw, exactly
print("2) delete-then-redraw: identical to the direct redraw")

# 3) a child-guide edit redefines Third space wholesale: BOTH boards drop
fresh_setup()
assert am.run_center_line_tool("child", am.H_PROPERTY, list(H2))
assert not lines("main")[0].get("third")
assert not lines("child")[0].get("third")
print("3) child guide edit: both boards' thirds retired")

# 4) first-ever guide install with no additional lines stays quiet
am._MAPPING_ASSETS.clear()
am._MAPPING_ASSETS["child"] = {}
am._MAPPING_ASSETS["main"] = {}
assert am.run_center_line_tool("main", am.H_PROPERTY, list(H))
print("4) fresh install without additional lines: no-op invalidation")


# 5) a child-guide edit writes the MAIN board's scriptData too - that
#    write must carry its own history commit, or a History-panel jump
#    on the main board resurrects the retired third (review finding)
class _Noop:
    def __getattr__(self, name):
        return _Noop()

    def __call__(self, *args, **kwargs):
        return None


commits = []


class _UI(_Noop):
    def history_commit(self, label, view):
        commits.append((label, view))


fresh_setup()
old_animean = am._animean
am._animean = lambda: types.SimpleNamespace(ui=_UI())
try:
    assert am.run_center_line_tool("child", am.H_PROPERTY, list(H2))
finally:
    am._animean = old_animean
assert ("Retire additional-line coordinates", "main") in commits, commits
assert not any(view == "child" and label.startswith("Retire")
               for label, view in commits), commits  # acting view: caller's
print("5) cross-board third-drop commits the other board's history")

print("t_third_invalidate: ALL OK")
