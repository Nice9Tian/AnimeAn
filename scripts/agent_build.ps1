$ErrorActionPreference = "Stop"

Write-Host "===== AGENT BUILD START ====="
Write-Host "PWD=$PWD"

$cmake = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
$buildDir = "C:\Users\admin\Documents\AnimeAn\build\Desktop_Qt_6_9_1_MinGW_64_bit-Release"

& $cmake --build $buildDir --target AnimeAn
$ec = $LASTEXITCODE

Write-Host "===== AGENT BUILD DONE EXIT_CODE=$ec ====="
exit $ec
