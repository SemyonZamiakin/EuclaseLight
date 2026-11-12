# Builds core/ into ui/EuclaseLight.App/native/qlcore.dll (x64, statically linked runtime) and runs tests.
$ErrorActionPreference = 'Stop'
$gpp = 'C:\msys64\mingw64\bin\g++.exe'
if (-not (Test-Path $gpp)) { $gpp = (Get-Command g++ -ErrorAction Stop).Source }
$env:PATH = (Split-Path $gpp) + ';' + $env:PATH
$root = $PSScriptRoot
$out = Join-Path $root 'ui\EuclaseLight.App\native'
New-Item -ItemType Directory -Force $out | Out-Null
$src = Get-ChildItem "$root\core\src\*.cpp" | ForEach-Object FullName
# Corundum (vendored, C11) is compiled as C and linked in
$gcc = Join-Path (Split-Path $gpp) 'gcc.exe'
$crd = Join-Path $root 'core\third_party\corundum\corundum.o'
& $gcc -std=c11 -O2 -Wall -Wextra -c "$root\core\third_party\corundum\corundum.c" -o $crd
if ($LASTEXITCODE) { throw 'corundum build failed' }
$src += $crd
& $gpp -std=c++17 -O2 -Wall -Wextra -shared -static -I"$root\core\include" $src -o "$out\qlcore.dll" -lws2_32
if ($LASTEXITCODE) { throw 'dll build failed' }
& $gpp -std=c++17 -O2 -Wall -Wextra -static -I"$root\core\include" "$root\core\tests\test_core.cpp" $src -o "$root\core\tests\test_core.exe" -lws2_32
if ($LASTEXITCODE) { throw 'test build failed' }
& "$root\core\tests\test_core.exe"
if ($LASTEXITCODE) { throw 'tests failed' }
