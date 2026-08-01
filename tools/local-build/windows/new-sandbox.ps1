# Generates a Windows Sandbox configuration for this checkout and launches it.
#
# One command from a prepared host: the sandbox opens, installs the cached
# toolchain, builds pimio, runs Darkroom and Studio, exports everything to the
# results folder on the host, and then stays open for Field Notes.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\local-build\windows\new-sandbox.ps1
#
# Options:
#   -CacheRoot <path>    Tool cache created by prepare.ps1.
#   -ResultsRoot <path>  Where results are exported. Defaults to
#                        <repo>\build\local-build\windows, which is git-ignored.
#   -MemoryInMB <n>      Sandbox memory. Defaults to 8192.
#   -NoStudio            Skip the Studio (Tests B) run inside the sandbox.
#   -NoLaunch            Write the .wsb file and print it, but do not start it.

[CmdletBinding()]
param(
    [string] $CacheRoot,
    [string] $ResultsRoot,
    [int] $MemoryInMB = 8192,
    [switch] $NoStudio,
    [switch] $NoLaunch
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'pinned.ps1')

$repositoryRoot = Get-PimioRepositoryRoot
Assert-PimioPinsMatchRepository -RepositoryRoot $repositoryRoot

if (-not $CacheRoot) {
    $CacheRoot = Get-PimioDefaultCacheRoot -RepositoryRoot $repositoryRoot
}
if (-not (Test-Path -LiteralPath (Join-Path $CacheRoot 'manifest.json'))) {
    throw "No prepared cache at $CacheRoot. Run tools\local-build\windows\prepare.ps1 first."
}

$sandboxExe = Join-Path $env:SystemRoot 'System32\WindowsSandbox.exe'
if (-not (Test-Path -LiteralPath $sandboxExe)) {
    throw "Windows Sandbox is not installed. Run prepare.ps1 -CheckOnly for the exact remedy."
}

if (-not $ResultsRoot) {
    $ResultsRoot = Join-Path $repositoryRoot 'build\local-build\windows'
}
$stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
$runResults = Join-Path $ResultsRoot $stamp
New-Item -ItemType Directory -Force -Path $runResults | Out-Null

# The bootstrap script is read from the mapped, read-only checkout, so the
# sandbox always runs the script that belongs to the commit being built.
$bootstrap = "$($PimioSandboxPaths.Source)\tools\local-build\windows\sandbox-bootstrap.ps1"
$bootstrapArguments = "-ExecutionPolicy Bypass -NoProfile -NoExit -File `"$bootstrap`""
if ($NoStudio) {
    $bootstrapArguments += ' -NoStudio'
}
$logonCommand = "powershell.exe $bootstrapArguments"

$template = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'pimio.wsb.template') -Raw
$configuration = $template.
    Replace('{{MEMORY_MB}}', $MemoryInMB.ToString()).
    Replace('{{HOST_SOURCE}}', $repositoryRoot).
    Replace('{{SANDBOX_SOURCE}}', $PimioSandboxPaths.Source).
    Replace('{{HOST_CACHE}}', (Resolve-Path $CacheRoot).Path).
    Replace('{{SANDBOX_CACHE}}', $PimioSandboxPaths.Cache).
    Replace('{{HOST_RESULTS}}', (Resolve-Path $runResults).Path).
    Replace('{{SANDBOX_RESULTS}}', $PimioSandboxPaths.Results).
    Replace('{{LOGON_COMMAND}}', $logonCommand)

# The configuration is kept with the results it produced: a build whose
# environment cannot be reconstructed is not evidence of anything.
$configurationPath = Join-Path $runResults 'pimio.wsb'
Set-Content -LiteralPath $configurationPath -Value $configuration -Encoding UTF8

Write-Host "Repository : $repositoryRoot (read-only in the sandbox)"
Write-Host "Cache      : $CacheRoot (read-only in the sandbox)"
Write-Host "Results    : $runResults (writable in the sandbox)"
Write-Host "Config     : $configurationPath"

if ($NoLaunch) {
    Write-Host ''
    Write-Host 'Not launching (-NoLaunch). Start it later with:'
    Write-Host "  `"$sandboxExe`" `"$configurationPath`""
    return
}

Write-Host ''
Write-Host 'Starting Windows Sandbox. The first run installs the Build Tools and takes a while;'
Write-Host 'watch the console inside the sandbox. Results appear in the folder above as they are'
Write-Host 'produced, and survive closing the sandbox.'
& $sandboxExe $configurationPath
