# Agent Instructions

## Purpose

Verify whether the deploy target builds and exits successfully.

## Build Policy

Builds are slow. Do not run a build unless it is necessary, such as after substantial code changes or when explicitly requested.

## Execution

- Shell: `powershell`
- Command:

```powershell
PowerShell -ExecutionPolicy Bypass -File ".\build_scripts\agent_build.ps1"
```

- Requires outside sandbox: yes
- Sandbox note: Build verification must run outside the sandbox because CMake/Qt autogen launches compiler and deployment subprocesses.

## Verification

- Expected termination log:

```text
===== AGENT BUILD DONE EXIT_CODE=... =====
```

- Required exit code: `0`
- Success criteria: An exit code of `0` indicates that the release target was successfully compiled and deployed.
