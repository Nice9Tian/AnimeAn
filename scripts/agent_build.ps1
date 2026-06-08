$ErrorActionPreference = "Stop"

$outPath = Join-Path -Path (Get-Location) -ChildPath "out.txt"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$logLock = New-Object System.Object

function Write-AgentLog {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Message
    )
    [System.Threading.Monitor]::Enter($script:logLock)
    try {
        [Console]::WriteLine($Message)
        [System.IO.File]::AppendAllText($script:outPath, "$Message`r`n", $script:utf8NoBom)
    } finally {
        [System.Threading.Monitor]::Exit($script:logLock)
    }
}

function Write-StepPassed {
    param([string]$Name)
    Write-AgentLog "${Name}:passed"
}

function Write-StepFailed {
    param([string]$Name, [string]$Message)
    Write-AgentLog "${Name}:failed $Message"
}

function Write-ProcessSnapshot {
    param([int]$RootProcessId)
    try {
        $processes = Get-CimInstance Win32_Process |
            Where-Object { $_.ProcessId -eq $RootProcessId -or $_.ParentProcessId -eq $RootProcessId } |
            Select-Object ProcessId, ParentProcessId, Name, CommandLine

        foreach ($processInfo in $processes) {
            Write-AgentLog "process_snapshot:passed pid=$($processInfo.ProcessId) parent=$($processInfo.ParentProcessId) name=$($processInfo.Name) command=$($processInfo.CommandLine)"
        }
    } catch {
        Write-StepFailed "process_snapshot" $_.Exception.Message
    }
}

$ec = 1

try {
    [System.IO.File]::WriteAllText($outPath, "", $utf8NoBom)
    Write-StepPassed "init_out_txt"
    Write-AgentLog "===== AGENT BUILD START ====="
    Write-AgentLog "PWD=$PWD"
    Write-AgentLog "OUT=$outPath"
    Write-StepPassed "write_header"

    $scriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
    $qtEnvLocationPath = Join-Path -Path $scriptDir -ChildPath "qt_env_loaction.ps1"
    if (-not (Test-Path -LiteralPath $qtEnvLocationPath -PathType Leaf)) { throw "Qt env location file not found: $qtEnvLocationPath" }
    . $qtEnvLocationPath
    Write-StepPassed "load_qt_env_location"

    $sourceDir = (Get-Location).Path
    $buildDir = Join-Path -Path $sourceDir -ChildPath "build\Desktop_Qt_6_9_1_MinGW_64_bit-Release"
    $generator = "MinGW Makefiles"
    $makeProgram = Join-Path -Path $mingwBin -ChildPath "mingw32-make.exe"
    $cCompiler = Join-Path -Path $mingwBin -ChildPath "gcc.exe"
    $cxxCompiler = Join-Path -Path $mingwBin -ChildPath "g++.exe"
    Write-StepPassed "set_paths"

    if (-not (Test-Path -LiteralPath $cmake -PathType Leaf)) { throw "cmake not found: $cmake" }
    Write-StepPassed "check_cmake_path"

    if (-not (Test-Path -LiteralPath $buildDir -PathType Container)) {
        New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
        Write-AgentLog "build_dir_created:passed path=$buildDir"
    }
    Write-StepPassed "check_build_dir"

    if (-not (Test-Path -LiteralPath $makeProgram -PathType Leaf)) { throw "mingw32-make not found: $makeProgram" }
    if (-not (Test-Path -LiteralPath $cCompiler -PathType Leaf)) { throw "gcc not found: $cCompiler" }
    if (-not (Test-Path -LiteralPath $cxxCompiler -PathType Leaf)) { throw "g++ not found: $cxxCompiler" }
    if (-not (Test-Path -LiteralPath $qtPrefixPath -PathType Container)) { throw "Qt prefix path not found: $qtPrefixPath" }
    Write-StepPassed "check_toolchain_paths"

    $env:Path = "$mingwBin;$env:Path"
    Write-StepPassed "set_mingw_path"

    # --- Step 1: clean the existing release directory ---
    $releaseSubDir = Join-Path -Path $buildDir -ChildPath "release"
    if (Test-Path -LiteralPath $releaseSubDir) {
        Write-AgentLog "Cleaning existing release directory..."
        Remove-Item -LiteralPath $releaseSubDir -Recurse -Force
        Write-StepPassed "clean_release_dir"
    }
    
    # Recreate an empty release directory.
    New-Item -ItemType Directory -Path $releaseSubDir -Force | Out-Null
    Write-StepPassed "recreate_release_dir"
    # -------------------------------------------

    $cachePath = Join-Path -Path $buildDir -ChildPath "CMakeCache.txt"
    $cmakeFilesPath = Join-Path -Path $buildDir -ChildPath "CMakeFiles"
    if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
        $cacheText = [System.IO.File]::ReadAllText($cachePath)
        if ($cacheText -match "CMAKE_GENERATOR:INTERNAL=NMake Makefiles") {
            Write-AgentLog "Cleaning stale NMake CMake cache..."
            Remove-Item -LiteralPath $cachePath -Force
            if (Test-Path -LiteralPath $cmakeFilesPath -PathType Container) {
                Remove-Item -LiteralPath $cmakeFilesPath -Recurse -Force
            }
            Write-StepPassed "clean_stale_cmake_cache"
        }
    }

    # --- Step 2: generate the CMake configuration cache (CMakeCache.txt) ---
    # Use an absolute source path so the script works from any working directory.
    $configArguments = "-S `"$sourceDir`" -B `"$buildDir`" -G `"$generator`" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=`"$qtPrefixPath`" -DCMAKE_MAKE_PROGRAM=`"$makeProgram`" -DCMAKE_C_COMPILER=`"$cCompiler`" -DCMAKE_CXX_COMPILER=`"$cxxCompiler`""
    Write-AgentLog "CONFIG_COMMAND=$cmake $configArguments"
    
    $configPsi = New-Object System.Diagnostics.ProcessStartInfo
    $configPsi.FileName = $cmake
    $configPsi.Arguments = $configArguments
    $configPsi.WorkingDirectory = $sourceDir
    $configPsi.UseShellExecute = $false
    $configPsi.RedirectStandardOutput = $true
    $configPsi.RedirectStandardError = $true
    $configPsi.CreateNoWindow = $true

    $configProcess = New-Object System.Diagnostics.Process
    $configProcess.StartInfo = $configPsi
    
    if (-not $configProcess.Start()) { throw "failed to start cmake for configuration" }
    
    $configOutput = $configProcess.StandardOutput.ReadToEnd()
    $configError = $configProcess.StandardError.ReadToEnd()
    $configProcess.WaitForExit()

    # Guard against null output before trimming.
    if ($null -ne $configOutput -and $configOutput.Trim() -ne "") { Write-AgentLog $configOutput }
    if ($null -ne $configError -and $configError.Trim() -ne "") { Write-AgentLog $configError }

    if ($configProcess.ExitCode -ne 0) {
        throw "CMake configuration failed. EXIT_CODE=$($configProcess.ExitCode)"
    }
    Write-StepPassed "cmake_configure"
    # ----------------------------------------------------

    # --- Step 3: run the build ---
    $arguments = "--build `"$buildDir`" --target deploy_AnimeAn --clean-first --verbose"
    Write-AgentLog "COMMAND=$cmake $arguments"
    Write-StepPassed "prepare_command"

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $cmake
    $psi.Arguments = $arguments
    $psi.WorkingDirectory = $sourceDir
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi

    if (-not $process.Start()) { throw "failed to start cmake" }
    Write-StepPassed "start_process"
    Write-AgentLog "PROCESS_ID=$($process.Id)"
    Write-StepPassed "begin_read_output"

    $outTask = $process.StandardOutput.ReadLineAsync()
    $errTask = $process.StandardError.ReadLineAsync()
    $outEof = $false
    $errEof = $false

    while ((-not $process.HasExited) -or (-not $outEof) -or (-not $errEof)) {
        $taskProcessed = $false

        if (-not $outEof -and $outTask.IsCompleted) {
            if ($null -ne $outTask.Result) {
                if ($outTask.Result.Trim() -ne "") {
                    Write-AgentLog $outTask.Result
                }
                $outTask = $process.StandardOutput.ReadLineAsync()
                $taskProcessed = $true
            } else {
                $outEof = $true
            }
        }

        if (-not $errEof -and $errTask.IsCompleted) {
            if ($null -ne $errTask.Result) {
                if ($errTask.Result.Trim() -ne "") {
                    Write-AgentLog $errTask.Result
                }
                $errTask = $process.StandardError.ReadLineAsync()
                $taskProcessed = $true
            } else {
                $errEof = $true
            }
        }

        if (-not $taskProcessed) {
            [System.Threading.Thread]::Sleep(10)
        }
    }

    $process.WaitForExit()
    Write-StepPassed "wait_process_exit"

    $ec = $process.ExitCode
    Write-StepPassed "read_exit_code"

    if ($ec -eq 0) {
        Write-AgentLog "deploy_AnimeAn:passed"
    } else {
        Write-StepFailed "deploy_AnimeAn" "EXIT_CODE=$ec"
    }

} catch {
    Write-StepFailed "agent_build" $_.Exception.Message
    $ec = 1
}

Write-StepPassed "write_footer"
Write-AgentLog "===== AGENT BUILD DONE EXIT_CODE=$ec ====="
exit $ec
