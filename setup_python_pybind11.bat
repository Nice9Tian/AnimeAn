@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul

REM ============================================================
REM setup_python_pybind11.bat
REM Put this file in your Qt/CMake project root, then run it.
REM It installs/sets up:
REM   - local Python:   tools\python312
REM   - local pybind11: external\pybind11
REM ============================================================

cd /d "%~dp0"
set "ROOT=%cd%"
set "PY_DIR=%ROOT%\tools\python312"
set "PY_EXE=%PY_DIR%\python.exe"
set "PY_VERSION=3.12.10"
set "PY_INSTALLER=%TEMP%\python-%PY_VERSION%-amd64.exe"
set "PY_INSTALLER_URL=https://www.python.org/ftp/python/%PY_VERSION%/python-%PY_VERSION%-amd64.exe"
set "FOUND_PY_DIR="
set "PYBIND_DIR=%ROOT%\external\pybind11"

echo.
echo [INFO] Project root:
echo        %ROOT%
echo.

REM Warn if project path has spaces. winget override is more fragile with spaces.
echo "%ROOT%" | findstr /C:" " >nul
if %errorlevel%==0 (
    echo [WARN] Your project path contains spaces.
    echo        If Python installation fails, move the project to a path without spaces and run again.
    echo.
)

REM ------------------------------------------------------------
REM 1. Install local Python 3.12 into tools\python312
REM ------------------------------------------------------------
if exist "%PY_EXE%" (
    echo [OK] Local Python already exists:
    echo      %PY_EXE%
) else (
    echo [INFO] Local Python not found. Installing Python %PY_VERSION% into this project...
    echo [INFO] TargetDir: %PY_DIR%
    echo.

    if not exist "%ROOT%\tools" mkdir "%ROOT%\tools"

    echo [INFO] Checking for an existing Python 3.12 that can be copied...
    for /f "usebackq delims=" %%P in (`py -3.12 -c "import os, sys; print(os.path.dirname(sys.executable))" 2^>nul`) do set "FOUND_PY_DIR=%%P"
    if defined FOUND_PY_DIR (
        if exist "!FOUND_PY_DIR!\python.exe" if exist "!FOUND_PY_DIR!\Include\Python.h" if exist "!FOUND_PY_DIR!\libs\python312.lib" (
            echo [INFO] Found existing Python:
            echo        !FOUND_PY_DIR!
            echo [INFO] Copying it to:
            echo        %PY_DIR%
            robocopy "!FOUND_PY_DIR!" "%PY_DIR%" /E /NFL /NDL /NJH /NJS /NP >nul
            if !errorlevel! LEQ 7 (
                echo [OK] Existing Python copied.
            ) else (
                echo [WARN] Copying existing Python failed. Continuing with installer fallback...
            )
        )
    )

    REM winget may skip installation when Python 3.12 already exists elsewhere.
    REM Only use winget / installer if no copyable Python 3.12 was found.
    if not exist "%PY_EXE%" (
        where winget >nul 2>nul
        if not errorlevel 1 (
            echo [INFO] Trying winget first...
            winget install --id Python.Python.3.12 -e --scope user --silent --accept-source-agreements --accept-package-agreements --override "/quiet InstallAllUsers=0 PrependPath=0 Include_pip=1 Include_dev=1 Include_lib=1 Include_test=0 TargetDir=%PY_DIR%"
            echo.
        )
    )

    if not exist "%PY_EXE%" (
        echo [INFO] winget did not create the local Python folder.
        echo [INFO] Downloading official Python installer...
        powershell -NoProfile -ExecutionPolicy Bypass -Command ^
            "$ErrorActionPreference='Stop';" ^
            "Invoke-WebRequest -Uri '%PY_INSTALLER_URL%' -OutFile '%PY_INSTALLER%';"

        if errorlevel 1 (
            echo [ERROR] Python installer download failed.
            echo         URL: %PY_INSTALLER_URL%
            echo         Please download it manually and install it to:
            echo         %PY_DIR%
            echo.
            pause
            exit /b 1
        )

        echo [INFO] Running Python installer with explicit TargetDir...
        "%PY_INSTALLER%" /quiet InstallAllUsers=0 PrependPath=0 Include_pip=1 Include_dev=1 Include_lib=1 Include_test=0 TargetDir="%PY_DIR%"
    )

    echo.
    if not exist "%PY_EXE%" (
        echo [ERROR] Python installation seems to have failed.
        echo         Expected file was not found:
        echo         %PY_EXE%
        echo.
        echo         You can manually install Python 3.12 Windows installer to this folder:
        echo         %PY_DIR%
        echo         Make sure Include\Python.h and libs\python312.lib exist.
        echo.
        pause
        exit /b 1
    )
)

echo.
echo [INFO] Checking Python files...
"%PY_EXE%" --version
if not exist "%PY_DIR%\Include\Python.h" (
    echo [ERROR] Missing: %PY_DIR%\Include\Python.h
    echo         Python development headers were not installed.
    pause
    exit /b 1
)
if not exist "%PY_DIR%\libs" (
    echo [ERROR] Missing: %PY_DIR%\libs
    echo         Python libraries were not installed.
    pause
    exit /b 1
)
echo [OK] Python.h found:
echo      %PY_DIR%\Include\Python.h
echo [OK] Python libs folder found:
echo      %PY_DIR%\libs

REM ------------------------------------------------------------
REM 2. Install / download pybind11 into external\pybind11
REM ------------------------------------------------------------
echo.
if exist "%PYBIND_DIR%\CMakeLists.txt" (
    echo [OK] pybind11 already exists:
    echo      %PYBIND_DIR%
) else (
    echo [INFO] pybind11 not found. Downloading to external\pybind11...
    if not exist "%ROOT%\external" mkdir "%ROOT%\external"

    if not exist "%PYBIND_DIR%\CMakeLists.txt" (
    where git >nul 2>nul
        if not errorlevel 1 (
            echo [INFO] Using git clone...
            git clone --depth 1 https://github.com/pybind/pybind11.git "%PYBIND_DIR%"
            if errorlevel 1 (
                echo [WARN] git clone failed. Trying PowerShell zip download...
                if exist "%PYBIND_DIR%" rmdir /s /q "%PYBIND_DIR%"
            )
        ) else (
            echo [INFO] Git was not found. Using PowerShell zip download...
        )
    )

    if not exist "%PYBIND_DIR%\CMakeLists.txt" (
        powershell -NoProfile -ExecutionPolicy Bypass -Command ^
            "$ErrorActionPreference='Stop';" ^
            "$zip=Join-Path $env:TEMP 'pybind11-master.zip';" ^
            "$out=Join-Path $env:TEMP 'pybind11_extract';" ^
            "if(Test-Path $zip){Remove-Item $zip -Force};" ^
            "if(Test-Path $out){Remove-Item $out -Recurse -Force};" ^
            "Invoke-WebRequest -Uri 'https://github.com/pybind/pybind11/archive/refs/heads/master.zip' -OutFile $zip;" ^
            "Expand-Archive -Path $zip -DestinationPath $out -Force;" ^
            "if(Test-Path '%PYBIND_DIR%'){Remove-Item '%PYBIND_DIR%' -Recurse -Force};" ^
            "Move-Item (Join-Path $out 'pybind11-master') '%PYBIND_DIR%';"
    )

    if not exist "%PYBIND_DIR%\CMakeLists.txt" (
        echo [ERROR] pybind11 download failed.
        echo         Please manually download pybind11 to:
        echo         %PYBIND_DIR%
        pause
        exit /b 1
    )
)

REM ------------------------------------------------------------
REM 3. Print CMake instructions
REM ------------------------------------------------------------
echo.
echo ============================================================
echo Done.
echo.
echo Python and pybind11 are ready in project-relative folders:
echo.
echo     tools\python312
echo     external\pybind11
echo.
echo IMPORTANT:
echo   The main CMakeLists.txt points directly to these folders.
echo   After changing dependencies, delete your build folder or clear CMake configuration in Qt Creator.
echo ============================================================
echo.
pause
endlocal
