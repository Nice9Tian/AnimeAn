$ErrorActionPreference = "Stop"

$outPath = Join-Path -Path (Get-Location) -ChildPath "out.txt"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$logLock = New-Object System.Object
$buildTimeoutSeconds = 300

if ($env:AGENT_BUILD_TIMEOUT_SECONDS) {
    $buildTimeoutSeconds = [int]$env:AGENT_BUILD_TIMEOUT_SECONDS
}

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
    Write-AgentLog "TIMEOUT_SECONDS=$buildTimeoutSeconds"
    Write-StepPassed "write_header"

    $cmake = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
    $buildDir = "C:\Users\admin\Documents\AnimeAn\build\Desktop_Qt_6_9_1_MinGW_64_bit-Release"
    Write-StepPassed "set_paths"

    if (-not (Test-Path -LiteralPath $cmake -PathType Leaf)) { throw "cmake not found: $cmake" }
    Write-StepPassed "check_cmake_path"

    if (-not (Test-Path -LiteralPath $buildDir -PathType Container)) { throw "build dir not found: $buildDir" }
    Write-StepPassed "check_build_dir"

    $arguments = "--build `"$buildDir`" --target deploy_AnimeAn --verbose"
    Write-AgentLog "COMMAND=$cmake $arguments"
    Write-StepPassed "prepare_command"

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $cmake
    $psi.Arguments = $arguments
    $psi.WorkingDirectory = (Get-Location).Path
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

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $lastSnapshotTime = 0

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

        $elapsedSeconds = [int]$stopwatch.Elapsed.TotalSeconds
        if ($elapsedSeconds - $lastSnapshotTime -ge 10) {
            if (-not $process.HasExited) {
                Write-AgentLog "wait_process:passed elapsed_seconds=$elapsedSeconds still_running=true"
                Write-ProcessSnapshot $process.Id
            }
            $lastSnapshotTime = $elapsedSeconds
        }

        if (-not $process.HasExited -and $elapsedSeconds -ge $buildTimeoutSeconds) {
            Write-StepFailed "build_timeout" "elapsed_seconds=$elapsedSeconds"
            Write-ProcessSnapshot $process.Id
            try {
                $children = Get-CimInstance Win32_Process |
                    Where-Object { $_.ParentProcessId -eq $process.Id } |
                    Select-Object -ExpandProperty ProcessId

                foreach ($childPid in $children) {
                    Stop-Process -Id $childPid -Force -ErrorAction SilentlyContinue
                    Write-AgentLog "stop_child_process:passed pid=$childPid"
                }
            } catch {
                Write-StepFailed "stop_child_process" $_.Exception.Message
            }

            $process.Kill()
            Write-StepPassed "stop_root_process"
            $process.WaitForExit()
            $ec = 124
            throw "build timed out after $elapsedSeconds seconds"
        }
    }

    $process.WaitForExit()
    $stopwatch.Stop()
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
    if ($ec -ne 124) {
        $ec = 1
    }
}

Write-StepPassed "write_footer"
Write-AgentLog "===== AGENT BUILD DONE EXIT_CODE=$ec ====="
exit $ec