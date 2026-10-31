# Opens the real Tauri window on the real backend, captures it, and compares it
# with the Qt baseline. Needs the web UI served on 1430 (npm run dev in webui).
#   powershell -ExecutionPolicy Bypass -File tests\ui_pixel\run_tauri.ps1 -Baseline qt.png -OutDir out
param(
    [Parameter(Mandatory = $true)][string]$Baseline,
    [Parameter(Mandatory = $true)][string]$OutDir,
    [string]$Shell = "$PSScriptRoot\..\..\src-tauri\target\debug\animean-shell.exe",
    [string]$Backend = "$PSScriptRoot\..\..\build\wt-check\backend\AnimeAnBackend.exe",
    [int]$WaitSeconds = 8
)
New-Item -ItemType Directory -Force $OutDir | Out-Null
$env:ANIMEAN_BACKEND_EXE = (Resolve-Path $Backend).Path
$env:PATH = "C:\Qt\6.9.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;$env:PATH"
$p = Start-Process -PassThru -FilePath (Resolve-Path $Shell).Path
try {
    Start-Sleep -Seconds $WaitSeconds
    py "$PSScriptRoot\capture_window.py" $p.Id "$OutDir\web.png"
} finally {
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
}
py "$PSScriptRoot\compare.py" $Baseline "$OutDir\web.png" --radius 10 --tolerance 10 --out-dir $OutDir
