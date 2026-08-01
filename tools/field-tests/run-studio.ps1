# Runs the Studio suite (Tests B): automated GUI tests that need a real
# display, so they cannot run in the project's headless CI. Run this from a
# desktop session on Windows, then send the archive it produces back with any
# bug report.
#
# Usage: powershell -ExecutionPolicy Bypass -File tools\field-tests\run-studio.ps1
# Requires: a source checkout, CMake >= 3.24, Ninja, MSVC (run from a
# "Developer PowerShell for VS" prompt), Qt 6 on PATH or CMAKE_PREFIX_PATH.
#
# Produces: pimio-studio-results-<timestamp>.zip in the repository root,
# containing the test log, JUnit XML, per-test logs, and screenshots.

$ErrorActionPreference = 'Stop'

$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
Set-Location $repo

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$results = Join-Path $repo "build\studio-results\$stamp"
New-Item -ItemType Directory -Force -Path $results | Out-Null

$env:PIMIO_STUDIO_RESULTS = $results

Write-Host '== Studio tests: configure and build =='
cmake --preset default
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build --preset default
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host '== Studio tests: run on the native display =='
ctest --preset studio --output-junit (Join-Path $results 'studio-junit.xml') `
    --output-log (Join-Path $results 'studio-ctest.log')
$status = $LASTEXITCODE

@(
    'pimio Studio (Tests B) run'
    "date: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"
    "host: $env:COMPUTERNAME, $([System.Environment]::OSVersion.VersionString)"
    "exit status: $status"
    "commit: $(git rev-parse HEAD 2>$null)"
) | Set-Content (Join-Path $results 'environment.txt')

$archive = Join-Path $repo "pimio-studio-results-$stamp.zip"
Compress-Archive -Path "$results\*" -DestinationPath $archive -Force

Write-Host ''
Write-Host "Results bundled into: $archive"
Write-Host 'Attach that file to a GitHub issue at https://github.com/dtmland/pimio/issues'
exit $status
