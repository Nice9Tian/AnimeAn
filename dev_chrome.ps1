param(
    [ValidateSet("start", "status", "stop", "print-config")]
    [string]$Action = "start",
    [int]$Port = 3432,
    [string]$Token = "animean-dev",
    [int]$FrontendPort = 1430,
    [string]$BackendExe = "",
    [switch]$NoVite,
    [switch]$NoBrowser,
    [switch]$Restart
)

$ErrorActionPreference = "Stop"

$nodeCmd = Get-Command node -ErrorAction SilentlyContinue
if (-not $nodeCmd) {
    throw "Node.js ('node') was not found on PATH."
}

$cliScript = Join-Path $PSScriptRoot "tools\mcp-backend-debug\cli.mjs"
if (-not (Test-Path $cliScript)) {
    throw "CLI script not found: $cliScript"
}

$argsList = @($cliScript, $Action, "--repo-root", $PSScriptRoot)

if ($Action -eq "start") {
    $argsList += @("--port", "$Port", "--token", $Token, "--frontend-port", "$FrontendPort")
    if ($BackendExe) {
        $argsList += @("--backend-exe", $BackendExe)
    }
    if ($NoVite) {
        $argsList += "--no-vite"
    }
    if (-not $NoBrowser) {
        $argsList += "--open-chrome"
    }
    if ($Restart) {
        $argsList += "--restart"
    }
}

& $nodeCmd.Source @argsList
exit $LASTEXITCODE
