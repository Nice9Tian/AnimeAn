"""Third-party library loader for the embedded Python runtime.

The app ships a FULL python312 (site-packages + pip), so tool code is
free to use real libraries instead of hand-rolled algorithms (user
directive). This module makes that survive a fresh or offline machine:

- `pywheels/` in the repository holds version-pinned wheels
  (requirements.txt beside them); CMake's deploy target and
  sync_pyfiles.ps1 copy the folder next to every deployed exe.
- `ensure(*modules)` self-installs anything missing on FIRST USE, from
  the bundled wheels (`pip --no-index --find-links pywheels`), falling
  back to the network only if no bundled wheel matches. It runs pip in
  the runtime's own python.exe (sys.executable is the host app inside
  the embedded interpreter) and retries the import afterwards.

Callers treat it as an import helper:

    earcut = pydeps.ensure("mapbox_earcut")   # None if unavailable
"""

import importlib
import os
import subprocess
import sys

_ATTEMPTED = set()


def _runtime_python():
    """The embedded runtime's own python.exe (sys.executable is the app)."""
    for base in (sys.prefix, sys.exec_prefix):
        candidate = os.path.join(base, "python.exe")
        if os.path.isfile(candidate):
            return candidate
    return None


def _wheel_dirs():
    """Bundled wheel folders, nearest first: beside the exe (deployed),
    then the source tree (development runs)."""
    candidates = []
    exe_dir = os.path.dirname(sys.executable or "")
    if exe_dir:
        candidates.append(os.path.join(exe_dir, "pywheels"))
    here = os.path.dirname(os.path.abspath(__file__))
    candidates.append(os.path.join(os.path.dirname(here), "pywheels"))
    candidates.append(os.path.join(here, "pywheels"))
    return [path for path in candidates if os.path.isdir(path)]


def ensure(module_name, pip_name=None):
    """Import `module_name`, self-installing it on first miss.

    Returns the module, or None when it cannot be provided (no bundled
    wheel, no network) - callers keep a degraded path for that case.
    """
    try:
        return importlib.import_module(module_name)
    except ImportError:
        pass
    key = module_name
    if key in _ATTEMPTED:
        return None
    _ATTEMPTED.add(key)
    python = _runtime_python()
    if python is None:
        print(f"[pydeps] cannot self-install {module_name}: no python.exe "
              "in the runtime prefix")
        return None
    package = pip_name or module_name
    commands = []
    for wheel_dir in _wheel_dirs():
        commands.append([python, "-m", "pip", "install", "--no-index",
                         "--find-links", wheel_dir, package])
    commands.append([python, "-m", "pip", "install", package])  # online
    for command in commands:
        try:
            result = subprocess.run(
                command, capture_output=True, text=True, timeout=300)
        except Exception as error:
            print(f"[pydeps] pip launch failed: {error}")
            continue
        if result.returncode == 0:
            importlib.invalidate_caches()
            try:
                module = importlib.import_module(module_name)
                print(f"[pydeps] installed {package} "
                      f"({'offline wheels' if '--no-index' in command else 'network'})")
                return module
            except ImportError:
                continue
    print(f"[pydeps] could not provide {package}; using the built-in "
          "fallback where one exists")
    return None
