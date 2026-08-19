# Scripts-only "build": copy edited Python scripts to every AnimeAn.exe
# directory (build Debug/Release, dist) without compiling anything.
#
# Why this exists: the CMake post-build step copies these files, but it only
# runs when a config is actually BUILT - editing a .py alone leaves the other
# configs and dist with stale copies. On the dev machine main.cpp now prefers
# the source pyfile dir, but dist must be self-contained, and a stale exe-side
# copy once shadowed a fix and masqueraded as an algorithm bug (commit dbe1850).
#
# Usage:  powershell -ExecutionPolicy Bypass -File sync_pyfiles.ps1

$root = Split-Path -Parent $MyInvocation.MyCommand.Path

# Same sources the CMake post-build copy list ships from.
$sources = @(Get-ChildItem (Join-Path $root "pyfile\*.py"))
foreach ($extra in @("pythonbind\animemodel.py", "opentoonz_tools\toonz_to_dict.py")) {
    $path = Join-Path $root $extra
    if (Test-Path $path) { $sources += Get-Item $path }
}

# Every directory that holds an AnimeAn.exe is a deployment to keep fresh.
$destinations = @()
foreach ($tree in @("build", "dist")) {
    $base = Join-Path $root $tree
    if (Test-Path $base) {
        $destinations += Get-ChildItem $base -Filter "AnimeAn.exe" -Recurse -ErrorAction SilentlyContinue |
            ForEach-Object { $_.DirectoryName }
    }
}
$destinations = $destinations | Sort-Object -Unique

if (-not $destinations) {
    Write-Host "No AnimeAn.exe found under build\ or dist\ - nothing to sync."
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
# Scripts are only half the picture: a .py fix cannot show up in an AnimeAn.exe
# that predates the C++ it depends on, and a stale exe next to fresh scripts is
# how "the fix does not work" reports start. Say so loudly rather than let the
# next test run against yesterday's build.
$newestSource = (Get-ChildItem -Path (Join-Path $root "*.cpp"), (Join-Path $root "*.h"),
                                     (Join-Path $root "algorithm\*"), (Join-Path $root "childrenpanel\*"),
                                     (Join-Path $root "pythonbind\*") -File -ErrorAction SilentlyContinue |
                  Sort-Object LastWriteTime -Descending | Select-Object -First 1)
if ($newestSource) {
    foreach ($dest in $destinations) {
        $exe = Join-Path $dest "AnimeAn.exe"
        if ((Test-Path $exe) -and (Get-Item $exe).LastWriteTime -lt $newestSource.LastWriteTime) {
            Write-Host ""
            Write-Host ("STALE BINARY: {0}" -f $exe)
            Write-Host ("  built {0}, but {1} changed {2}" -f (Get-Item $exe).LastWriteTime,
                        $newestSource.Name, $newestSource.LastWriteTime)
            Write-Host "  C++ changed since this exe was built - rebuild it (and run the"
            Write-Host "  deploy_AnimeAn target for dist\) before testing."
        }
    }
}
