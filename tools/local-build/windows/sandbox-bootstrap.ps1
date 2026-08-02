# Runs inside Windows Sandbox: turn a clean image plus a read-only cache into a
# built, tested, staged pimio, and leave the evidence on the host.
#
# new-sandbox.ps1 starts this script as the sandbox logon command. It is not
# meant to be run on a developer machine: it installs a compiler and writes to
# fixed C:\pimio paths, which is only reasonable on a machine that is discarded
# when the window closes.
#
# Order matters here. Nothing is exported before Darkroom has run, because the
# rule for this environment is that a failed test means no local build package.

[CmdletBinding()]
param(
    [string] $Source = 'C:\pimio\source',
    [string] $Cache = 'C:\pimio\cache',
    [string] $Results = 'C:\pimio\results',
    [string] $Work = 'C:\pimio\work',
    [string] $Tools = 'C:\pimio\tools',
    [switch] $NoStudio
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

. (Join-Path $Source 'tools\local-build\windows\pinned.ps1')

New-Item -ItemType Directory -Force -Path $Results, $Work, $Tools | Out-Null
Start-Transcript -Path (Join-Path $Results 'bootstrap.log') -Append | Out-Null

$started = Get-Date
$status = [ordered]@{
    darkroom = 'not run'
    studio   = if ($NoStudio) { 'skipped' } else { 'not run' }
    stage    = 'not run'
}

function Write-Step {
    param([Parameter(Mandatory = $true)][string] $Message)
    Write-Host ''
    Write-Host "== $Message" -ForegroundColor Cyan
}

function Expand-Tool {
    <#
    .SYNOPSIS
        Extracts a cached zip into the sandbox tool directory once.
    #>
    param(
        [Parameter(Mandatory = $true)][string] $Archive,
        [Parameter(Mandatory = $true)][string] $Destination
    )
    if (Test-Path -LiteralPath $Destination) {
        return $Destination
    }
    if (-not (Test-Path -LiteralPath $Archive)) {
        throw "Missing cached archive: $Archive. Re-run prepare.ps1 on the host."
    }
    Expand-Archive -LiteralPath $Archive -DestinationPath $Destination -Force
    return $Destination
}

function Import-VcVars {
    <#
    .SYNOPSIS
        Imports an MSVC developer environment into this PowerShell session.
    .DESCRIPTION
        vcvars64.bat only knows how to configure a cmd session, so it is run in
        one and the resulting environment is copied back. Without this, CMake
        finds no compiler and the build fails at configure time.
    #>
    param(
        [Parameter(Mandatory = $true)][string] $VcVarsPath
    )
    if (-not (Test-Path -LiteralPath $VcVarsPath)) {
        throw "Missing $VcVarsPath. The Build Tools install did not complete."
    }
    $output = cmd /c "`"$VcVarsPath`" >nul 2>&1 && set"
    foreach ($line in $output) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "env:$($Matches[1])" -Value $Matches[2]
        }
    }
}

function Invoke-Step {
    <#
    .SYNOPSIS
        Runs a command, records its exit code, and reports it without aborting.
    .DESCRIPTION
        A failing test run still has to export its logs, so failures are carried
        in $status and summarised at the end rather than thrown.
    #>
    param(
        [Parameter(Mandatory = $true)][string] $Name,
        [Parameter(Mandatory = $true)][scriptblock] $Action
    )
    & $Action | Out-Host
    $code = $LASTEXITCODE
    if ($code -eq 0) {
        Write-Host "${Name}: passed" -ForegroundColor Green
        return 'passed'
    }
    Write-Host "${Name}: FAILED (exit $code)" -ForegroundColor Red
    return "failed (exit $code)"
}

try {
    Write-Step 'Toolchain from the cache'
    $downloads = Join-Path $Cache 'downloads'
    $cmakeRoot = Expand-Tool -Archive (Join-Path $downloads "cmake-$($PimioPinned.CMakeVersion)-windows-x86_64.zip") `
        -Destination (Join-Path $Tools 'cmake')
    $cmakeBin = Join-Path (Get-ChildItem -LiteralPath $cmakeRoot -Directory | Select-Object -First 1).FullName 'bin'
    $ninjaBin = Expand-Tool -Archive (Join-Path $downloads "ninja-$($PimioPinned.NinjaVersion)-win.zip") `
        -Destination (Join-Path $Tools 'ninja')

    $qtPrefix = Join-Path $Cache "qt\$($PimioPinned.QtVersion)\$($PimioPinned.QtHostDir)"
    if (-not (Test-Path -LiteralPath (Join-Path $qtPrefix 'bin\qmake.exe'))) {
        throw "Qt $($PimioPinned.QtVersion) is not in the cache at $qtPrefix. Re-run prepare.ps1 without -SkipQt."
    }

    $env:PATH = "$cmakeBin;$ninjaBin;$(Join-Path $qtPrefix 'bin');$env:PATH"
    Write-Host "cmake : $((Get-Command cmake).Source)"
    Write-Host "ninja : $((Get-Command ninja).Source)"
    Write-Host "qt    : $qtPrefix"

    Write-Step 'Visual Studio Build Tools'
    $vcVars = 'C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path -LiteralPath $vcVars)) {
        $bootstrapper = Join-Path $downloads 'vs_BuildTools.exe'
        $arguments = @('--quiet', '--wait', '--norestart', '--nocache', '--installPath', 'C:\BuildTools')
        foreach ($component in $PimioPinned.VsComponents) {
            $arguments += @('--add', $component)
        }
        Write-Host 'Installing the C++ build tools. This is the slow part of a first run.'
        $process = Start-Process -FilePath $bootstrapper -ArgumentList $arguments -Wait -PassThru
        # 3010 is "success, reboot requested"; the tools are usable without it.
        if ($process.ExitCode -ne 0 -and $process.ExitCode -ne 3010) {
            throw "The Visual Studio Build Tools installer failed with exit code $($process.ExitCode)."
        }
    }
    Import-VcVars -VcVarsPath $vcVars
    Write-Host "cl    : $((Get-Command cl -ErrorAction SilentlyContinue).Source)"

    Write-Step 'Working copy'
    # The checkout is mapped read-only on purpose, so the build happens on a
    # copy. Generated directories are excluded rather than copied and deleted:
    # a stale build/ from the host would silently change what is tested.
    $null = robocopy $Source $Work /MIR /NFL /NDL /NJH /NJS /NP /XD 'build' '.cache' 'node_modules'
    if ($LASTEXITCODE -ge 8) {
        throw "Copying the source into the sandbox failed (robocopy exit $LASTEXITCODE)."
    }
    $global:LASTEXITCODE = 0

    # The cache is read-only, but CMake extracts LORE next to its archive, so
    # the archives are placed in the working copy's own cache first.
    $loreCache = Join-Path $Work '.cache\lore'
    foreach ($bundle in $PimioPinned.LoreBundles) {
        $relative = Get-PimioLoreCacheRelativePath -Bundle $bundle.Bundle
        $from = Join-Path (Join-Path $Cache 'lore') $relative
        $to = Join-Path $loreCache $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
        Copy-Item -LiteralPath $from -Destination $to -Force
    }

    Set-Location $Work

    Write-Step 'Configure and build'
    cmake --preset default -DPIMIO_REQUIRE_LORE=ON "-DCMAKE_PREFIX_PATH=$qtPrefix"
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --preset default failed with exit code $LASTEXITCODE."
    }
    cmake --build --preset default
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --build --preset default failed with exit code $LASTEXITCODE."
    }

    Write-Step 'Darkroom (Tests A)'
    $status.darkroom = Invoke-Step -Name 'Darkroom' -Action {
        ctest --preset default --output-on-failure `
            --output-junit (Join-Path $Results 'darkroom-junit.xml')
    }

    if (-not $NoStudio) {
        Write-Step 'Studio (Tests B) on the sandbox desktop'
        $studioResults = Join-Path $Results 'studio'
        New-Item -ItemType Directory -Force -Path $studioResults | Out-Null
        $env:PIMIO_STUDIO_RESULTS = $studioResults
        $status.studio = Invoke-Step -Name 'Studio' -Action {
            ctest --preset studio --output-on-failure `
                --output-junit (Join-Path $Results 'studio-junit.xml')
        }
    }

    Copy-Item -Path (Join-Path $Work 'build\default\Testing\Temporary\*.log') `
        -Destination $Results -ErrorAction SilentlyContinue

    Write-Step 'Stage the application'
    if ($status.darkroom -ne 'passed') {
        # Stated policy, enforced here: a failed Darkroom run means there is no
        # local build package to hand to anyone.
        $status.stage = 'skipped: Darkroom did not pass'
        Write-Host $status.stage -ForegroundColor Yellow
    } else {
        $stage = Join-Path $Work 'stage'
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
        cmake --install (Join-Path $Work 'build\default') --prefix $stage
        if ($LASTEXITCODE -ne 0) {
            throw "cmake --install failed with exit code $LASTEXITCODE."
        }
        $archive = Join-Path $Results 'pimio-windows-x64.zip'
        Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
        $status.stage = "staged: $archive"
        Write-Host $status.stage -ForegroundColor Green
    }
} catch {
    Write-Host ''
    Write-Host "Bootstrap failed: $($_.Exception.Message)" -ForegroundColor Red
    $status['error'] = $_.Exception.Message
} finally {
    Write-Step 'Environment record'
    $git = Get-Command git -ErrorAction SilentlyContinue
    $commit = $null
    if ($git) {
        $commit = (& git -C $Work rev-parse HEAD 2>$null)
    }
    if (-not $commit) { $commit = 'unknown (no git in the sandbox)' }
    $record = @(
        'pimio local build (Windows Sandbox)'
        "started : $($started.ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"
        "finished: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"
        "commit  : $commit"
        "windows : $((Get-CimInstance Win32_OperatingSystem).Caption) build $((Get-CimInstance Win32_OperatingSystem).BuildNumber)"
        "qt      : $($PimioPinned.QtVersion) $($PimioPinned.QtArch)"
        "lore    : $($PimioPinned.LoreVersion)"
        "cmake   : $($PimioPinned.CMakeVersion)"
        "ninja   : $($PimioPinned.NinjaVersion)"
        'commands: cmake --preset default -DPIMIO_REQUIRE_LORE=ON; cmake --build --preset default; ctest --preset default; ctest --preset studio; cmake --install build\default'
    )
    foreach ($key in $status.Keys) {
        $record += "$key : $($status[$key])"
    }
    $record | Set-Content -LiteralPath (Join-Path $Results 'environment.txt') -Encoding UTF8

    Write-Step 'Summary'
    $record | ForEach-Object { Write-Host $_ }
    Write-Host ''
    Write-Host "Everything above is in $Results and survives closing this sandbox."

    # Field Notes are manual by definition, so the checklist is opened rather
    # than automated, and the sandbox is left running for them.
    $fieldNotes = Join-Path $Work 'docs\plan\manual-testing.md'
    if (Test-Path -LiteralPath $fieldNotes) {
        Start-Process notepad.exe $fieldNotes
    }
    Write-Host 'Field Notes (Tests C) are open in Notepad. The staged application is under'
    Write-Host "$Work\stage. Close the sandbox when you are done; the results folder is kept."
    Stop-Transcript | Out-Null
}
