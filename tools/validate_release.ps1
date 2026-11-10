param(
    [string]$Python = 'python',
    [string]$Output = 'build/release-validation'
)
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
if (Test-Path $Output) { throw "Output already exists: $Output" }
if (-not (Get-Command dumpbin -ErrorAction SilentlyContinue)) { throw 'Run from an x64 Visual Studio developer shell with dumpbin on PATH.' }
New-Item -ItemType Directory -Path $Output | Out-Null
$Output = (Resolve-Path $Output).Path
$native = Join-Path $Output 'native'
$stage = Join-Path $Output 'stage'
$wheelDir = Join-Path $Output 'wheel'
$assets = Join-Path $Output 'assets'
function Run([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
Run cmake @('-S', '.', '-B', $native, '-DABC_PROFILE=OFF')
Run cmake @('--build', $native, '--config', 'Release', '--parallel', '4')
Run ctest @('--test-dir', $native, '-C', 'Release', '--output-on-failure')
$dll = Join-Path $native 'Release/abcurves.dll'
$env:ABCURVES_LIBRARY = $dll
$env:PYTHONPATH = (Resolve-Path 'API/python/src').Path
$env:CARGO_TARGET_DIR = Join-Path $Output 'cargo'
Run $Python @('-m', 'pytest', 'API/python/tests', 'API/tools/tests', '-q')
Run cargo @('test', '--manifest-path', 'API/rust/Cargo.toml', '--release')
Run $Python @('tools/check_publication.py')
Run $Python @('API/tools/package_windows.py', '--dll', $dll, '--out', $wheelDir)
$wheel = @(Get-ChildItem $wheelDir -Filter '*.whl')
if ($wheel.Count -ne 1) { throw 'Expected exactly one wheel.' }
Run $Python @('tools/publication.py', 'runtime', '--dll', $dll, '--out', $stage)
Run $Python @('tools/test_runtime_package.py', $stage, '--wheel', $wheel[0].FullName)
Run $Python @('tools/release_archives.py', $stage, '--out', $assets, '--wheel', $wheel[0].FullName)
& dumpbin /DEPENDENTS $dll | Out-File -Encoding utf8 (Join-Path $Output 'dependencies.txt')
if ($LASTEXITCODE -ne 0) { throw 'dumpbin dependency inspection failed' }
& dumpbin /EXPORTS $dll | Out-File -Encoding utf8 (Join-Path $Output 'exports.txt')
if ($LASTEXITCODE -ne 0) { throw 'dumpbin export inspection failed' }
$exports = Get-Content (Join-Path $Output 'exports.txt') -Raw
$header = Get-Content 'API/c/include/abcurves.h' -Raw
$names = [regex]::Matches($header, 'ABC_CALL\s+(abc_\w+)\s*\(') | ForEach-Object { $_.Groups[1].Value }
foreach ($name in $names) { if ($exports -notmatch "\b$name\b") { throw "Missing export: $name" } }
foreach ($line in Get-Content (Join-Path $assets 'SHA256SUMS.txt')) {
    $parts = $line -split '  ', 2
    if ((Get-FileHash (Join-Path $assets $parts[1]) -Algorithm SHA256).Hash -ne $parts[0]) { throw "Bad package hash: $($parts[1])" }
}
Write-Host "Release validation passed. Artifacts: $Output"
