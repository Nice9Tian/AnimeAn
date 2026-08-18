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
