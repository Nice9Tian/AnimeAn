$ErrorActionPreference = "Stop"

$ec = 1
$tailAlreadyWritten = $false

# =========================
# Low-output settings
# =========================
# false: passed steps only go to build_full.log
# true : passed steps also go to console/out.txt
$ShowPassedSteps = $false

# When configure/build fails, copy only the last N lines from build_full.log to console/out.txt
$FailureTailLines = 80

# =========================
# Paths
# =========================
$sourceDir = (Get-Location).ProviderPath
$outPath = Join-Path -Path $sourceDir -ChildPath "out.txt"
$fullLogPath = Join-Path -Path $sourceDir -ChildPath "build_full.log"

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$logLock = New-Object System.Object

$buildDir = Join-Path -Path $sourceDir -ChildPath "build\Desktop_Qt_6_9_1_MinGW_64_bit-Release"

# =========================
# Cross-process build queue
# =========================
# Same build dir => same queue.
# Different checkout/build dir => independent queue.
$sha256 = [System.Security.Cryptography.SHA256]::Create()
try {
    $lockHash = -join (
        $sha256.ComputeHash(
            [System.Text.Encoding]::UTF8.GetBytes($buildDir.ToLowerInvariant())
        ) | ForEach-Object { $_.ToString("x2") }
    )
} finally {
    $sha256.Dispose()
}

# Global\ works across Windows sessions.
# If all agents run in the same user session, Local\ is also enough.
$mutexName = "Global\AnimeAnAgentBuildMutex_$lockHash"
$mutex = $null
$hasMutex = $false

# =========================
# Logging helpers
# =========================
function Write-AgentLog {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Message,

        [switch]$Console,

        [switch]$FullOnly,

        [switch]$SummaryOnly
    )

    [System.Threading.Monitor]::Enter($script:logLock)
    try {
        if (-not $SummaryOnly) {
            [System.IO.File]::AppendAllText($script:fullLogPath, "$Message`r`n", $script:utf8NoBom)
        }

        if (-not $FullOnly) {
            [System.IO.File]::AppendAllText($script:outPath, "$Message`r`n", $script:utf8NoBom)
        }

        if ($Console) {
            [Console]::WriteLine($Message)
        }
    } finally {
        [System.Threading.Monitor]::Exit($script:logLock)
    }
}

function Write-AgentSummary {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Message,

        [switch]$DoNotWriteFullLog
    )

    if ($DoNotWriteFullLog) {
        Write-AgentLog -Message $Message -Console -SummaryOnly
    } else {
        Write-AgentLog -Message $Message -Console
    }
}

function Write-FullLog {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    Write-AgentLog -Message $Message -FullOnly
}

function Write-StepPassed {
    param([string]$Name)

    if ($script:ShowPassedSteps) {
        Write-AgentSummary "${Name}:passed"
    } else {
        Write-FullLog "${Name}:passed"
    }
}

function Write-StepFailed {
    param([string]$Name, [string]$Message)

    Write-AgentSummary "${Name}:failed $Message"
}

function Write-FullLogTailToSummary {
    param(
        [int]$Lines = 80
    )

    try {
        if (Test-Path -LiteralPath $script:fullLogPath -PathType Leaf) {
            Write-AgentSummary "----- build_full.log last $Lines lines -----" -DoNotWriteFullLog

            Get-Content -LiteralPath $script:fullLogPath -Tail $Lines | ForEach-Object {
                Write-AgentSummary $_ -DoNotWriteFullLog
            }

            Write-AgentSummary "----- end build_full.log tail -----" -DoNotWriteFullLog
        }
    } catch {
        Write-AgentSummary "log_tail:failed $($_.Exception.Message)"
    }
}

function Write-ProcessSnapshot {
    param([int]$RootProcessId)

    try {
        $processes = Get-CimInstance Win32_Process |
            Where-Object { $_.ProcessId -eq $RootProcessId -or $_.ParentProcessId -eq $RootProcessId } |
            Select-Object ProcessId, ParentProcessId, Name, CommandLine

        foreach ($processInfo in $processes) {
            Write-FullLog "process_snapshot:passed pid=$($processInfo.ProcessId) parent=$($processInfo.ParentProcessId) name=$($processInfo.Name) command=$($processInfo.CommandLine)"
        }
    } catch {
        Write-StepFailed "process_snapshot" $_.Exception.Message
    }
}

try {
    # Create cross-process mutex.
    $mutex = New-Object System.Threading.Mutex($false, $mutexName)

    # Wait in queue before touching out.txt, build dir, release dir, or CMake cache.
    $queueStart = [DateTimeOffset]::Now
    [Console]::WriteLine("build_queue:waiting name=$mutexName")

    try {
        $hasMutex = $mutex.WaitOne(-1)
    } catch [System.Threading.AbandonedMutexException] {
        # Previous holder crashed or was killed.
        # Windows has released the mutex, and this process now owns it.
        $hasMutex = $true
        [Console]::WriteLine("build_queue:abandoned_mutex_recovered name=$mutexName")
    }

    $waitedSeconds = [int](([DateTimeOffset]::Now - $queueStart).TotalSeconds)

    # From here on, this process is the only builder for this build directory.
    [System.IO.File]::WriteAllText($outPath, "", $utf8NoBom)
    [System.IO.File]::WriteAllText($fullLogPath, "", $utf8NoBom)

    Write-AgentSummary "build_queue:entered waited=${waitedSeconds}s name=$mutexName"
    Write-AgentSummary "===== AGENT BUILD START ====="
    Write-AgentSummary "PWD=$PWD"
    Write-AgentSummary "OUT=$outPath"
    Write-AgentSummary "FULL_LOG=$fullLogPath"
    Write-StepPassed "write_header"

    $scriptDir = if ($PSScriptRoot) {
        $PSScriptRoot
    } else {
        Split-Path -Parent $MyInvocation.MyCommand.Path
    }

    $qtEnvLocationPath = Join-Path -Path $scriptDir -ChildPath "qt_env_location.ps1"
    if (-not (Test-Path -LiteralPath $qtEnvLocationPath -PathType Leaf)) {
        throw "Qt env location file not found: $qtEnvLocationPath"
    }

    . $qtEnvLocationPath
    Write-StepPassed "load_qt_env_location"

    $generator = "MinGW Makefiles"
    $makeProgram = Join-Path -Path $mingwBin -ChildPath "mingw32-make.exe"
    $cCompiler = Join-Path -Path $mingwBin -ChildPath "gcc.exe"
    $cxxCompiler = Join-Path -Path $mingwBin -ChildPath "g++.exe"
    Write-StepPassed "set_paths"

    if (-not (Test-Path -LiteralPath $cmake -PathType Leaf)) {
        throw "cmake not found: $cmake"
    }
    Write-StepPassed "check_cmake_path"

    if (-not (Test-Path -LiteralPath $buildDir -PathType Container)) {
        New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
        Write-FullLog "build_dir_created:passed path=$buildDir"
    }
    Write-StepPassed "check_build_dir"

    if (-not (Test-Path -LiteralPath $makeProgram -PathType Leaf)) {
        throw "mingw32-make not found: $makeProgram"
    }

    if (-not (Test-Path -LiteralPath $cCompiler -PathType Leaf)) {
        throw "gcc not found: $cCompiler"
    }

    if (-not (Test-Path -LiteralPath $cxxCompiler -PathType Leaf)) {
        throw "g++ not found: $cxxCompiler"
    }

    if (-not (Test-Path -LiteralPath $qtPrefixPath -PathType Container)) {
        throw "Qt prefix path not found: $qtPrefixPath"
    }

    Write-StepPassed "check_toolchain_paths"

    $env:Path = "$mingwBin;$env:Path"
    Write-StepPassed "set_mingw_path"

    # =========================
    # Step 1: clean release dir
    # =========================
    $releaseSubDir = Join-Path -Path $buildDir -ChildPath "release"

    if (Test-Path -LiteralPath $releaseSubDir) {
        Write-AgentSummary "clean_release_dir:running"
        Remove-Item -LiteralPath $releaseSubDir -Recurse -Force
        Write-StepPassed "clean_release_dir"
    }

    New-Item -ItemType Directory -Path $releaseSubDir -Force | Out-Null
    Write-StepPassed "recreate_release_dir"

    # =========================
    # Clean stale CMake cache
    # =========================
    $cachePath = Join-Path -Path $buildDir -ChildPath "CMakeCache.txt"
    $cmakeFilesPath = Join-Path -Path $buildDir -ChildPath "CMakeFiles"

    if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
        $cacheText = [System.IO.File]::ReadAllText($cachePath)

        if ($cacheText -match "CMAKE_GENERATOR:INTERNAL=NMake Makefiles") {
            Write-AgentSummary "clean_stale_cmake_cache:running"
            Remove-Item -LiteralPath $cachePath -Force

            if (Test-Path -LiteralPath $cmakeFilesPath -PathType Container) {
                Remove-Item -LiteralPath $cmakeFilesPath -Recurse -Force
            }

            Write-StepPassed "clean_stale_cmake_cache"
        }
    }

    # =========================
    # Step 2: CMake configure
    # =========================
    $configArguments = "-S `"$sourceDir`" -B `"$buildDir`" -G `"$generator`" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=`"$qtPrefixPath`" -DCMAKE_MAKE_PROGRAM=`"$makeProgram`" -DCMAKE_C_COMPILER=`"$cCompiler`" -DCMAKE_CXX_COMPILER=`"$cxxCompiler`""

    Write-AgentSummary "cmake_configure:running"
    Write-FullLog "CONFIG_COMMAND=$cmake $configArguments"

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

    if (-not $configProcess.Start()) {
        throw "failed to start cmake for configuration"
    }

    $configOutputTask = $configProcess.StandardOutput.ReadToEndAsync()
    $configErrorTask = $configProcess.StandardError.ReadToEndAsync()

    $configProcess.WaitForExit()

    $configOutput = $configOutputTask.Result
    $configError = $configErrorTask.Result

    if ($null -ne $configOutput -and $configOutput.Trim() -ne "") {
        Write-FullLog $configOutput
    }

    if ($null -ne $configError -and $configError.Trim() -ne "") {
        Write-FullLog $configError
    }

    if ($configProcess.ExitCode -ne 0) {
        Write-StepFailed "cmake_configure" "EXIT_CODE=$($configProcess.ExitCode)"
        Write-FullLogTailToSummary -Lines $FailureTailLines
        $tailAlreadyWritten = $true
        throw "CMake configuration failed. EXIT_CODE=$($configProcess.ExitCode)"
    }

    Write-StepPassed "cmake_configure"

    # =========================
    # Step 3: Build
    # =========================
    # Intentionally no --verbose to avoid massive logs/context usage.
    $arguments = "--build `"$buildDir`" --target deploy_AnimeAn --clean-first"

    Write-AgentSummary "deploy_AnimeAn:running"
    Write-FullLog "COMMAND=$cmake $arguments"
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

    if (-not $process.Start()) {
        throw "failed to start cmake"
    }

    Write-StepPassed "start_process"
    Write-FullLog "PROCESS_ID=$($process.Id)"
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
                    Write-FullLog $outTask.Result
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
                    Write-FullLog $errTask.Result
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
        Write-AgentSummary "deploy_AnimeAn:passed"
    } else {
        Write-StepFailed "deploy_AnimeAn" "EXIT_CODE=$ec"
        Write-FullLogTailToSummary -Lines $FailureTailLines
        $tailAlreadyWritten = $true
    }

} catch {
    $ec = 1
    $message = $_.Exception.Message

    if ($hasMutex) {
        try {
            Write-StepFailed "agent_build" $message

            if (-not $tailAlreadyWritten) {
                Write-FullLogTailToSummary -Lines $FailureTailLines
                $tailAlreadyWritten = $true
            }
        } catch {
            [Console]::Error.WriteLine("agent_build:failed $message")
            [Console]::Error.WriteLine("agent_build:failed_to_write_log $($_.Exception.Message)")
        }
    } else {
        [Console]::Error.WriteLine("agent_build:failed before entering build queue $message")
    }

} finally {
    if ($hasMutex) {
        try {
            Write-StepPassed "write_footer"
            Write-AgentSummary "===== AGENT BUILD DONE EXIT_CODE=$ec ====="
            Write-AgentSummary "FULL_LOG=$fullLogPath"
            Write-AgentSummary "build_queue:leaving name=$mutexName"
        } catch {
            [Console]::Error.WriteLine("write_footer:failed $($_.Exception.Message)")
        }

        try {
            $mutex.ReleaseMutex()
        } catch {
            [Console]::Error.WriteLine("build_queue:release_failed $($_.Exception.Message)")
        }
    }

    if ($null -ne $mutex) {
        $mutex.Dispose()
    }
}

exit $ec