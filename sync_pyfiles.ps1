# Scripts-only "build": copy edited Python scripts beside every built
# AnimeAnBackend.exe under build\ and dist\ without compiling anything.
#
# Why this exists: only a full package (build_scripts\agent_build.ps1) refreshes
# the copies beside the backend, so editing a .py alone leaves dist with stale
# copies. On the dev machine the backend reads the source folders first
# (backend/main.cpp), but a release folder has no source tree and runs its own
# copies, and a stale exe-side copy once shadowed a fix and masqueraded as an
# algorithm bug (commit dbe1850).
#
# Usage:  powershell -ExecutionPolicy Bypass -File sync_pyfiles.ps1

$root = Split-Path -Parent $MyInvocation.MyCommand.Path

# Same sources build_scripts\package_tauri.ps1 ships.
$sources = @(Get-ChildItem (Join-Path $root "pyfile\*.py"))
foreach ($extra in @("pythonbind\animemodel.py", "opentoonz_tools\toonz_to_dict.py")) {
    $path = Join-Path $root $extra
    if (Test-Path $path) { $sources += Get-Item $path }
}

# The backend imports the copies beside itself, so every directory that holds an
# AnimeAnBackend.exe is a deployment to keep fresh (Qt-era build folders with
# only an old AnimeAn.exe are left alone).
$destinations = @()
foreach ($tree in @("build", "dist")) {
    $base = Join-Path $root $tree
    if (Test-Path $base) {
        $destinations += Get-ChildItem $base -Filter "AnimeAnBackend.exe" -Recurse -ErrorAction SilentlyContinue |
            ForEach-Object { $_.DirectoryName }
    }
}
$destinations = $destinations | Sort-Object -Unique

if (-not $destinations) {
    Write-Host "No AnimeAnBackend.exe found under build\ or dist\ - nothing to sync."
    exit 0
}

$updated = 0
foreach ($dest in $destinations) {
    foreach ($src in $sources) {
        $target = Join-Path $dest $src.Name
        $copy = -not (Test-Path $target)
        if (-not $copy) {
            $copy = (Get-FileHash $src.FullName).Hash -ne (Get-FileHash $target).Hash
        }
        if ($copy) {
            try {
                Copy-Item $src.FullName $target -Force -ErrorAction Stop
                Write-Host ("updated: {0} -> {1}" -f $src.Name, $dest)
                $updated++
            } catch {
                Write-Host ("FAILED:  {0} -> {1}  ({2})" -f $src.Name, $dest, $_.Exception.Message)
            }
        }
    }
}

Write-Host ("{0} file(s) updated across {1} deployment(s)." -f $updated, $destinations.Count)

# --- bundled wheels ----------------------------------------------------------
# pywheels\ holds the version-pinned wheels for the embedded runtime's
# third-party libraries (see pyfile\pydeps.py). Copy it beside every exe so a
# fresh or OFFLINE machine can self-install on first use.
$wheelSrc = Join-Path $root "pywheels"
if (Test-Path $wheelSrc) {
    foreach ($dest in $destinations) {
        $wheelDst = Join-Path $dest "pywheels"
        New-Item -ItemType Directory -Force $wheelDst | Out-Null
        foreach ($wheel in Get-ChildItem $wheelSrc -File) {
            $target = Join-Path $wheelDst $wheel.Name
            $copy = -not (Test-Path $target)
            if (-not $copy) {
                $copy = (Get-FileHash $wheel.FullName).Hash -ne (Get-FileHash $target).Hash
            }
            if ($copy) {
                Copy-Item $wheel.FullName $target -Force
                Write-Host ("wheels:  {0} -> {1}" -f $wheel.Name, $wheelDst)
            }
        }
    }
}

# --- stale-binary warning ----------------------------------------------------
# Scripts are only half the picture: a .py fix cannot show up in a backend that
# predates the C++ it depends on, and a stale exe next to fresh scripts is how
# "the fix does not work" reports start. Say so loudly rather than let the next
# test run against yesterday's build.
$cppSources = @(Get-ChildItem -Path (Join-Path $root "*") -Include *.cpp, *.h, *.hpp -File -ErrorAction SilentlyContinue)
foreach ($dir in @("backend", "algorithm", "pythonbind")) {
    $cppSources += Get-ChildItem -Path (Join-Path $root $dir) -Recurse -Include *.cpp, *.h, *.hpp -File -ErrorAction SilentlyContinue
}
$newestSource = $cppSources | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($newestSource) {
    foreach ($dest in $destinations) {
        $exe = Join-Path $dest "AnimeAnBackend.exe"
        if ((Test-Path $exe) -and (Get-Item $exe).LastWriteTime -lt $newestSource.LastWriteTime) {
            Write-Host ""
            Write-Host ("STALE BINARY: {0}" -f $exe)
            Write-Host ("  built {0}, but {1} changed {2}" -f (Get-Item $exe).LastWriteTime,
                        $newestSource.Name, $newestSource.LastWriteTime)
            Write-Host "  C++ changed since this backend was built - rebuild it (for dist\, run"
            Write-Host "  build_scripts\agent_build.ps1) before testing."
        }
    }
}
