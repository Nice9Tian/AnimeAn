# Agent Instructions

开发规则、语义分级、验证基线与提交流程都在 `docs/developer_guide.md`；本文只保留构建验证的执行说明。

## Purpose

Verify whether the backend builds and `dist\AnimeAn\` is packaged successfully.

## Build Policy

Builds are slow. Do not run a build unless it is necessary, such as after substantial code changes or when explicitly requested.

## Execution

- Shell: `powershell`
- Command:

```powershell
PowerShell -ExecutionPolicy Bypass -File ".\build_scripts\agent_build.ps1"
```

- Requires outside sandbox: yes
- Sandbox note: Build verification must run outside the sandbox because CMake/MinGW, Vite (`npm run build`) and Cargo launch compiler and packaging subprocesses.

## On Linux

Linux is a development system only: there is no `dist` package to build there. The commands are in `docs/mechanism/build_deploy.md` under「在 Linux 上开发」.

- Shell: `bash`
- Build the backend and run the C++ tests:

```bash
build_scripts/linux_build.sh --test
```

- After a C++ or CMake change, check that it still builds for Windows:

```bash
build_scripts/windows_cross_check.sh
```

- Expected termination logs: `===== LINUX BUILD DONE: ... =====` and `===== WINDOWS CROSS CHECK DONE: ... =====`, both with exit code `0`.
- The release package and its verification below still need a Windows machine.

## Verification

- Expected termination log:

```text
===== AGENT BUILD DONE EXIT_CODE=... =====
```

- Required exit code: `0`
- Success criteria: An exit code of `0` indicates that the backend, web UI and shell were built and `dist\AnimeAn\` was packaged.
