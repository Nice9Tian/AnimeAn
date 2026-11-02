<#
.SYNOPSIS
    组装可独立运行的桌面版发布文件夹 dist/AnimeAn/。

.DESCRIPTION
    按顺序完成以下步骤，生成脱离源码树、换机即用的自包含发布包 dist/AnimeAn/：
    1. 在 webui/ 目录下执行 npm run build，生成前端静态产物 webui/dist/。
    2. 执行 cargo build --release --manifest-path src-tauri/Cargo.toml（启用 tauri/custom-protocol 内嵌前端产物），
       将生成的 animean-shell.exe 改名为 AnimeAn.exe 放入 dist/AnimeAn/。
    3. 从 -BackendDir 指定的后端构建目录复制 AnimeAnBackend.exe，以及其同目录下的全部 .dll 与已有的 Python 相关运行文件。
    4. 部署后端所需的 MinGW 运行库（libgcc_s_seh-1.dll、libstdc++-6.dll、libwinpthread-1.dll）。
    5. 依据 backend/CMakeLists.txt 与 backend/main.cpp 的路径查找规则补齐自包含 Python 环境与脚本：
       - backend/main.cpp 启动时在 <appDir>/python312 下查找 python312.dll 设置 PYTHONHOME，
         且 AnimeAnBackend.exe 进程加载时直接依赖同目录的 python312.dll / vcruntime140*.dll；
       - 无源码树机器上编译期宏 ANIMEAN_PYFILE_DIR / ANIMEAN_PYTHONBIND_DIR / ANIMEAN_OPENTOONZ_TOOLS_DIR 不存在，
         PYTHONPATH 只含 <appDir>，initalize.py 与 pydeps.py 从 <appDir> 及其下 pywheels/ 查找模块和离线 wheel，
         animeanbusiness.cpp 从 <appDir>/childrenpanel/tool_controls/ 查找兜底选项布局。
       因此脚本将 -PythonHome 复制到 dist/AnimeAn/python312/，将 python312.dll 等复制到根部，
       并将仓库中的 pyfile/*.py、pythonbind/animemodel.py、opentoonz_tools/toonz_to_dict.py、
       pywheels/、childrenpanel/tool_controls/ 一并复制到发布目录。

.PARAMETER BackendDir
    包含 AnimeAnBackend.exe 的目录路径（例如 build/Release/backend）。
    若不传则自动在仓库的 build/ 下查找。

.PARAMETER PythonHome
    Python 3.12 运行时目录路径（包含 python.exe、python312.dll、Lib/、DLLs/ 等）。
    若不传则默认使用仓库下的 tools/python312。

.PARAMETER MingwBin
    MinGW 工具链 bin 目录，默认 C:/Qt/Tools/mingw1310_64/bin。

.PARAMETER OutputDir
    输出发布目录，默认 <仓库根目录>/dist/AnimeAn。

.EXAMPLE
    PowerShell -ExecutionPolicy Bypass -File .\build_scripts\package_tauri.ps1 `
        -BackendDir .\build\Release\backend `
        -PythonHome .\tools\python312
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$BackendDir = "",

    [Parameter(Mandatory = $false)]
    [string]$PythonHome = "",

    [Parameter(Mandatory = $false)]
    [string]$MingwBin = "C:/Qt/Tools/mingw1310_64/bin",

    [Parameter(Mandatory = $false)]
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

$scriptDir = if ($PSScriptRoot) {
    $PSScriptRoot
} else {
    Split-Path -Parent $MyInvocation.MyCommand.Path
}
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..")).ProviderPath

# 若存在 toolchain_location.ps1 且调用方未显式覆盖默认路径，则读取其中的默认配置
$toolchainFile = Join-Path $scriptDir "toolchain_location.ps1"
if (Test-Path -LiteralPath $toolchainFile -PathType Leaf) {
    . $toolchainFile
    if (-not $PSBoundParameters.ContainsKey("MingwBin") -and $mingwBin) {
        $MingwBin = $mingwBin
    }
}

function Resolve-ExistingDirectory {
    param(
        [string]$InputPath,
        [string]$BaseDir
    )
    if ([string]::IsNullOrWhiteSpace($InputPath)) {
        return $null
    }
    if (Test-Path -LiteralPath $InputPath -PathType Container) {
        return (Resolve-Path -LiteralPath $InputPath).ProviderPath
    }
    $combined = Join-Path $BaseDir $InputPath
    if (Test-Path -LiteralPath $combined -PathType Container) {
        return (Resolve-Path -LiteralPath $combined).ProviderPath
    }
    return $null
}

# 1. 解析 -BackendDir
$resolvedBackendDir = Resolve-ExistingDirectory -InputPath $BackendDir -BaseDir $repoRoot
if ($BackendDir -and -not $resolvedBackendDir) {
    throw "-BackendDir 指定的目录不存在: '$BackendDir'。"
}
if (-not $resolvedBackendDir) {
    $candidates = @(
        (Join-Path $repoRoot "build\Release\backend"),
        (Join-Path $repoRoot "build\Release"),
        (Join-Path $repoRoot "build\wt-check\backend"),
        (Join-Path $repoRoot "build\backend")
    )
    foreach ($cand in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $cand "AnimeAnBackend.exe") -PathType Leaf) {
            $resolvedBackendDir = (Resolve-Path -LiteralPath $cand).ProviderPath
            break
        }
    }
}

if (-not $resolvedBackendDir) {
    throw "未找到后端目录，请通过 -BackendDir 指定包含 AnimeAnBackend.exe 的目录（已检查参数: '$BackendDir'）。"
}

if (-not (Test-Path -LiteralPath (Join-Path $resolvedBackendDir "AnimeAnBackend.exe") -PathType Leaf)) {
    foreach ($sub in @("backend", "release", "backend\release")) {
        $subDir = Join-Path $resolvedBackendDir $sub
        if (Test-Path -LiteralPath (Join-Path $subDir "AnimeAnBackend.exe") -PathType Leaf) {
            $resolvedBackendDir = (Resolve-Path -LiteralPath $subDir).ProviderPath
            break
        }
    }
}

$backendExeSrc = Join-Path $resolvedBackendDir "AnimeAnBackend.exe"
if (-not (Test-Path -LiteralPath $backendExeSrc -PathType Leaf)) {
    throw "在 -BackendDir ($resolvedBackendDir) 下未找到 AnimeAnBackend.exe。"
}

# 2. 解析 -PythonHome
$resolvedPythonHome = Resolve-ExistingDirectory -InputPath $PythonHome -BaseDir $repoRoot
if ($PythonHome -and -not $resolvedPythonHome) {
    throw "-PythonHome 指定的目录不存在: '$PythonHome'。"
}
if (-not $resolvedPythonHome) {
    $defaultPyHome = Join-Path $repoRoot "tools\python312"
    if (Test-Path -LiteralPath $defaultPyHome -PathType Container) {
        $resolvedPythonHome = (Resolve-Path -LiteralPath $defaultPyHome).ProviderPath
    }
}

if (-not $resolvedPythonHome -or -not (Test-Path -LiteralPath (Join-Path $resolvedPythonHome "python312.dll") -PathType Leaf)) {
    throw "未找到有效的 Python 3.12 运行时目录（需包含 python312.dll），请通过 -PythonHome 指定（当前: '$PythonHome'）。"
}

# 3. 解析输出目录 dist/AnimeAn/
$distDir = if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    Join-Path $repoRoot "dist\AnimeAn"
} elseif ([System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir
} else {
    Join-Path $repoRoot $OutputDir
}

Write-Host "===== Package AnimeAn (Tauri) ====="
Write-Host "RepoRoot   : $repoRoot"
Write-Host "BackendDir : $resolvedBackendDir"
Write-Host "PythonHome : $resolvedPythonHome"
Write-Host "MingwBin   : $MingwBin"
Write-Host "OutputDir  : $distDir"

# =========================
# 步骤 1：在 webui 里 npm run build
# =========================
$webuiDir = Join-Path $repoRoot "webui"
if (-not (Test-Path -LiteralPath (Join-Path $webuiDir "package.json") -PathType Leaf)) {
    throw "未找到 webui/package.json: $webuiDir"
}

Write-Host "[1/6] Building frontend in webui (npm run build)..."
Push-Location -LiteralPath $webuiDir
try {
    & npm run build
    if ($LASTEXITCODE -ne 0) {
        throw "webui npm run build 失败，退出码: $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

$webuiDist = Join-Path $webuiDir "dist"
if (-not (Test-Path -LiteralPath (Join-Path $webuiDist "index.html") -PathType Leaf)) {
    throw "前端构建后未找到 webui/dist/index.html。"
}

# =========================
# 步骤 2：编译 Tauri 外壳（Release）
# =========================
$manifestPath = Join-Path $repoRoot "src-tauri\Cargo.toml"
Write-Host "[2/6] Building Tauri shell (cargo build --release --manifest-path src-tauri/Cargo.toml)..."
& cargo build --release --manifest-path $manifestPath --features tauri/custom-protocol
if ($LASTEXITCODE -ne 0) {
    throw "cargo build --release 失败，退出码: $LASTEXITCODE"
}

$cargoTarget = if ($env:CARGO_TARGET_DIR) { $env:CARGO_TARGET_DIR } else { Join-Path $repoRoot "src-tauri\target" }
$builtShellExe = Join-Path $cargoTarget "release\animean-shell.exe"
if (-not (Test-Path -LiteralPath $builtShellExe -PathType Leaf)) {
    throw "未找到编译后的外壳程序: $builtShellExe"
}

# =========================
# 步骤 3：初始化 dist/AnimeAn 并放入 AnimeAn.exe 与后端文件
# =========================
Write-Host "[3/6] Preparing $distDir and copying executables..."
if (Test-Path -LiteralPath $distDir) {
    Remove-Item -LiteralPath $distDir -Recurse -Force
}
New-Item -ItemType Directory -Path $distDir -Force | Out-Null

# animean-shell.exe -> AnimeAn.exe
Copy-Item -LiteralPath $builtShellExe -Destination (Join-Path $distDir "AnimeAn.exe") -Force

# 复制 AnimeAnBackend.exe
Copy-Item -LiteralPath $backendExeSrc -Destination (Join-Path $distDir "AnimeAnBackend.exe") -Force

# 复制 -BackendDir 下 AnimeAnBackend.exe 旁边的 dll
Get-ChildItem -LiteralPath $resolvedBackendDir -Filter "*.dll" -File -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $distDir $_.Name) -Force
}

Get-ChildItem -LiteralPath $resolvedBackendDir -Filter "*.py" -File -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $distDir $_.Name) -Force
}

foreach ($pyRelDir in @("pywheels", "python312", "childrenpanel")) {
    $srcRel = Join-Path $resolvedBackendDir $pyRelDir
    if (Test-Path -LiteralPath $srcRel -PathType Container) {
        Copy-Item -LiteralPath $srcRel -Destination (Join-Path $distDir $pyRelDir) -Recurse -Force
    }
}

# =========================
# 步骤 4：部署 MinGW 运行库
# =========================
Write-Host "[4/6] Deploying MinGW runtime DLLs..."
$mingwRuntimeDlls = @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")
foreach ($dll in $mingwRuntimeDlls) {
    $targetDll = Join-Path $distDir $dll
    if (-not (Test-Path -LiteralPath $targetDll -PathType Leaf)) {
        $fromMingw = Join-Path $MingwBin $dll
        if (Test-Path -LiteralPath $fromMingw -PathType Leaf) {
            Copy-Item -LiteralPath $fromMingw -Destination $targetDll -Force
        } else {
            throw "缺少 MinGW 运行库: $dll（在 $MingwBin 中未找到）"
        }
    }
}

# =========================
# 步骤 5：复制 Python 3.12 运行时（-PythonHome -> dist/AnimeAn/python312）
# =========================
# backend/main.cpp 在 <appDir>/python312 下查找 python312.dll 并设为 PYTHONHOME；
# 同时 AnimeAnBackend.exe 链接了 python312.dll，需要在同目录放置 python312.dll 等动态库。
Write-Host "[5/6] Copying Python runtime from $resolvedPythonHome..."
$distPythonHome = Join-Path $distDir "python312"
if (Test-Path -LiteralPath $distPythonHome) {
    Remove-Item -LiteralPath $distPythonHome -Recurse -Force
}
Copy-Item -LiteralPath $resolvedPythonHome -Destination $distPythonHome -Recurse -Force

foreach ($pyDll in @("python312.dll", "python3.dll", "vcruntime140.dll", "vcruntime140_1.dll")) {
    $srcDll = Join-Path $resolvedPythonHome $pyDll
    if (Test-Path -LiteralPath $srcDll -PathType Leaf) {
        Copy-Item -LiteralPath $srcDll -Destination (Join-Path $distDir $pyDll) -Force
    }
}

# =========================
# 步骤 6：从仓库补齐 pyfile、pythonbind、opentoonz_tools、pywheels 与 tool_controls
# =========================
# 发布的文件夹没有源码树，backend/main.cpp 只把 <appDir>（即 dist/AnimeAn）放上 PYTHONPATH，
# initalize.py 也在 <appDir> 里找，所以全部 .py 都放在 <appDir> 根目录，不另放子目录副本。
Write-Host "[6/6] Copying Python scripts, bindings, wheels, and tool controls from repository..."

$pyfileSrcDir = Join-Path $repoRoot "pyfile"
Get-ChildItem -LiteralPath $pyfileSrcDir -Filter "*.py" -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $distDir $_.Name) -Force
}

foreach ($extra in @("pythonbind\animemodel.py", "opentoonz_tools\toonz_to_dict.py")) {
    $extraSrc = Join-Path $repoRoot $extra
    if (Test-Path -LiteralPath $extraSrc -PathType Leaf) {
        Copy-Item -LiteralPath $extraSrc -Destination (Join-Path $distDir (Split-Path -Leaf $extraSrc)) -Force
    }
}

$pywheelsSrc = Join-Path $repoRoot "pywheels"
if (Test-Path -LiteralPath $pywheelsSrc -PathType Container) {
    $distPywheels = Join-Path $distDir "pywheels"
    if (Test-Path -LiteralPath $distPywheels) {
        Remove-Item -LiteralPath $distPywheels -Recurse -Force
    }
    Copy-Item -LiteralPath $pywheelsSrc -Destination $distPywheels -Recurse -Force
}

$toolControlsSrc = Join-Path $repoRoot "childrenpanel\tool_controls"
if (Test-Path -LiteralPath $toolControlsSrc -PathType Container) {
    $distToolControls = Join-Path $distDir "childrenpanel\tool_controls"
    New-Item -ItemType Directory -Path (Split-Path -Parent $distToolControls) -Force | Out-Null
    if (Test-Path -LiteralPath $distToolControls) {
        Remove-Item -LiteralPath $distToolControls -Recurse -Force
    }
    Copy-Item -LiteralPath $toolControlsSrc -Destination $distToolControls -Recurse -Force
}

# 校验关键产物齐全
$expectedPaths = @(
    "AnimeAn.exe",
    "AnimeAnBackend.exe",
    "libgcc_s_seh-1.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll",
    "python312.dll",
    "python312\python312.dll",
    "initalize.py",
    "hello_world.py",
    "animemodel.py",
    "toonz_to_dict.py",
    "pywheels"
)
foreach ($rel in $expectedPaths) {
    $full = Join-Path $distDir $rel
    if (-not (Test-Path -LiteralPath $full)) {
        throw "打包完整性校验失败，缺少文件或目录: $full"
    }
}

Write-Host "===== PACKAGE TAURI DONE: $distDir ====="
