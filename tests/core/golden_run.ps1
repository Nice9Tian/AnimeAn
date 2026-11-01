# Old-against-new check for the computation layer: builds golden_dump.cpp on a
# checkout from before the move to AnimeCore and on this tree, then compares
# every result. Run from the repository root:
#   powershell -ExecutionPolicy Bypass -File tests\core\golden_run.ps1 -OldTree <checkout with Qt types>
param([Parameter(Mandatory = $true)][string]$OldTree)
$env:PATH = "C:\Qt\6.9.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;" + $env:PATH
$new = (Get-Location).Path.Replace('\', '/')
$old = $OldTree.Replace('\', '/')
$qt = 'C:/Qt/6.9.1/mingw_64'
$src = "$new/tests/core/golden_dump.cpp"
$out = "$new/build/golden"
New-Item -ItemType Directory -Force $out | Out-Null

cmake -S algorithm/core -B build/core -G Ninja -DCMAKE_PREFIX_PATH=$qt -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF 2>&1 | Select-Object -Last 1
cmake --build build/core 2>&1 | Select-Object -Last 1

g++ -std=c++17 -O2 "-I$new" "-I$new/algorithm" -o "$out/golden_new.exe" $src "$new/build/core/libanimean_core.a" "$new/build/core/libanimean_corelib.a"
if ($LASTEXITCODE -ne 0) { "NEW BUILD FAILED"; exit 1 }

$oldSources = @("$old/algorithm/animemodel.cpp", "$old/algorithm/beziersplit.cpp", "$old/algorithm/scenehistory.cpp", "$old/algorithm/vectorlogic.cpp", "$old/projectio.cpp")
g++ -std=c++17 -O2 -DGOLDEN_OLD "-I$old" "-I$old/algorithm" "-I$qt/include" "-I$qt/include/QtCore" "-I$qt/include/QtGui" -o "$out/golden_old.exe" $src @oldSources "-L$qt/lib" -lQt6Gui -lQt6Core
if ($LASTEXITCODE -ne 0) { "OLD BUILD FAILED"; exit 1 }

& "$out/golden_new.exe" "$out/new.txt"
& "$out/golden_old.exe" "$out/old.txt"
py tests/core/golden_compare.py "$out/old.txt" "$out/new.txt"
exit $LASTEXITCODE
