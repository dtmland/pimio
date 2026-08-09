# Prepares a Windows host to build pimio in Windows Sandbox.
#
# Windows Sandbox starts from a clean image every time and keeps nothing, so the
# toolchain has to come from somewhere the sandbox can read instantly. This
# script checks the host prerequisites once and fills a developer-owned cache
# with the pinned, checksum-verified vendor downloads the sandbox will install
# from. Nothing here is committed and nothing here is redistributed: every file
# is fetched from its own vendor by the developer who will use it.
#
# Usage:
#   tools\local-build\windows\prepare.bat
#   powershell -ExecutionPolicy Bypass -File tools\local-build\windows\prepare.ps1
#
# Options:
#   -CacheRoot <path>   Where to keep the downloads. Defaults to
#                       <repo>\.cache\local-build\windows, which is git-ignored.
#   -SkipQt             Do not download Qt. The sandbox build needs Qt, so use
#                       this only to refresh the other artifacts.
#   -Force              Re-download everything even when the cache is valid.
#   -CheckOnly          Report prerequisites and cache state, download nothing.
#
# The script is safe to re-run: an artifact whose recorded SHA-256 still matches
# is left alone, so an interrupted run resumes rather than starting over.

[CmdletBinding()]
param(
    [string] $CacheRoot,
    [switch] $SkipQt,
    [switch] $Force,
    [switch] $CheckOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

. (Join-Path $PSScriptRoot 'pinned.ps1')

$repositoryRoot = Get-PimioRepositoryRoot
Assert-PimioPinsMatchRepository -RepositoryRoot $repositoryRoot

if (-not $CacheRoot) {
    $CacheRoot = Get-PimioDefaultCacheRoot -RepositoryRoot $repositoryRoot
}

$downloads = Join-Path $CacheRoot 'downloads'
$loreCache = Join-Path $CacheRoot 'lore'
$qtCache = Join-Path $CacheRoot 'qt'

function Write-Step {
    param([Parameter(Mandatory = $true)][string] $Message)
    Write-Host ''
    Write-Host "== $Message" -ForegroundColor Cyan
}

function Get-FileSha256 {
    param([Parameter(Mandatory = $true)][string] $Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Test-HostPrerequisites {
    <#
    .SYNOPSIS
        Reports every host requirement, and returns the blocking failures.
    .DESCRIPTION
        The checks are reported individually rather than as one pass/fail so a
        developer can see exactly which requirement is missing, which is the
        difference between "turn on a Windows feature" and "this edition of
        Windows cannot do this at all".
    #>
    $problems = @()

    $os = Get-CimInstance -ClassName Win32_OperatingSystem
    Write-Host "Windows       : $($os.Caption) build $($os.BuildNumber)"
    if ($os.Caption -match 'Home') {
        $problems += 'Windows Sandbox is not available on Windows Home. A Pro, Enterprise, or Education edition is required.'
    }

    $sandboxExe = Join-Path $env:SystemRoot 'System32\WindowsSandbox.exe'
    $featureState = 'unknown'
    try {
        $feature = Get-WindowsOptionalFeature -Online -FeatureName 'Containers-DisposableClientVM' -ErrorAction Stop
        $featureState = $feature.State
    } catch {
        # Get-WindowsOptionalFeature needs an elevated session; the executable
        # test below is the fallback that does not.
        $featureState = 'not queried (run as administrator to query it)'
    }
    Write-Host "Sandbox feature: $featureState"
    if (-not (Test-Path -LiteralPath $sandboxExe)) {
        $problems += "Windows Sandbox is not installed. Enable it with: Enable-WindowsOptionalFeature -Online -FeatureName 'Containers-DisposableClientVM' -All (elevated, then reboot)."
    }

    $virtualization = (Get-CimInstance -ClassName Win32_ComputerSystem).HypervisorPresent
    Write-Host "Hypervisor    : $virtualization"
    if (-not $virtualization) {
        $problems += 'No hypervisor is present. Enable hardware virtualization in the firmware; Windows Sandbox cannot start without it.'
    }

    $free = (Get-PSDrive -Name ((Split-Path -Qualifier $CacheRoot).TrimEnd(':'))).Free
    $freeGb = [math]::Round($free / 1GB, 1)
    Write-Host "Free space    : $freeGb GB on $(Split-Path -Qualifier $CacheRoot)"
    if ($free -lt 25GB) {
        $problems += "At least 25 GB of free space is needed for the cache and the sandbox build; $freeGb GB is available."
    }

    $python = Get-Command 'python' -ErrorAction SilentlyContinue
    if (-not $python) {
        $python = Get-Command 'py' -ErrorAction SilentlyContinue
    }
    Write-Host "Python        : $(if ($python) { $python.Source } else { 'not found' })"
    if (-not $python -and -not $SkipQt) {
        $problems += 'Python 3 is required to download Qt with aqtinstall. Install it from https://www.python.org/downloads/windows/ or run with -SkipQt.'
    }

    return @{ Problems = $problems; Python = $python }
}

function Save-VerifiedFile {
    <#
    .SYNOPSIS
        Downloads a file once and proves it is the file that was pinned.
    .DESCRIPTION
        A cached file whose hash still matches is kept, so re-running the script
        after an interrupted download costs nothing. BITS is preferred because
        it resumes a partial transfer; Invoke-WebRequest is the fallback when
        the service is unavailable.
    #>
    param(
        [Parameter(Mandatory = $true)][string] $Url,
        [Parameter(Mandatory = $true)][string] $Path,
        [string] $ExpectedSha256
    )

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null

    if ((Test-Path -LiteralPath $Path) -and -not $Force) {
        if (-not $ExpectedSha256) {
            Write-Host "  cached (vendor-controlled, hash recorded): $(Split-Path -Leaf $Path)"
            return (Get-FileSha256 -Path $Path)
        }
        $actual = Get-FileSha256 -Path $Path
        if ($actual -eq $ExpectedSha256) {
            Write-Host "  cached and verified: $(Split-Path -Leaf $Path)"
            return $actual
        }
        Write-Host "  cached copy does not match its recorded checksum, downloading again: $(Split-Path -Leaf $Path)"
        Remove-Item -LiteralPath $Path -Force
    }

    Write-Host "  downloading $Url"
    $downloaded = $false
    if (Get-Command 'Start-BitsTransfer' -ErrorAction SilentlyContinue) {
        try {
            Start-BitsTransfer -Source $Url -Destination $Path -ErrorAction Stop
            $downloaded = $true
        } catch {
            Write-Host "  BITS transfer failed ($($_.Exception.Message)); falling back to Invoke-WebRequest"
        }
    }
    if (-not $downloaded) {
        Invoke-WebRequest -Uri $Url -OutFile $Path -UseBasicParsing
    }

    $actual = Get-FileSha256 -Path $Path
    if ($ExpectedSha256 -and $actual -ne $ExpectedSha256) {
        Remove-Item -LiteralPath $Path -Force
        throw "Checksum mismatch for $Url. Expected $ExpectedSha256, got $actual. Nothing was kept."
    }
    return $actual
}

function Install-CachedQt {
    <#
    .SYNOPSIS
        Downloads the pinned Qt into the cache with aqtinstall.
    #>
    param(
        [Parameter(Mandatory = $true)] $Python
    )

    $installed = Join-Path $qtCache "$($PimioPinned.QtVersion)\$($PimioPinned.QtHostDir)"
    if ((Test-Path -LiteralPath (Join-Path $installed 'bin\qmake.exe')) -and -not $Force) {
        Write-Host "  cached: Qt $($PimioPinned.QtVersion) at $installed"
        return $installed
    }

    Write-Host "  installing $($PimioPinned.AqtInstall) for the current user"
    & $Python.Source -m pip install --user --disable-pip-version-check --quiet $PimioPinned.AqtInstall
    if ($LASTEXITCODE -ne 0) {
        throw "Could not install $($PimioPinned.AqtInstall)."
    }

    $arguments = @(
        '-m', 'aqt', 'install-qt', 'windows', 'desktop',
        $PimioPinned.QtVersion, $PimioPinned.QtArch,
        '--outputdir', $qtCache
    )
    if ($PimioPinned.QtModules.Count -gt 0) {
        $arguments += '--modules'
        $arguments += $PimioPinned.QtModules
    }

    Write-Host "  downloading Qt $($PimioPinned.QtVersion) $($PimioPinned.QtArch)"
    & $Python.Source @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "aqtinstall failed to install Qt $($PimioPinned.QtVersion)."
    }

    if (-not (Test-Path -LiteralPath (Join-Path $installed 'bin\qmake.exe'))) {
        throw "Qt was downloaded but $installed does not look like a Qt installation."
    }
    return $installed
}

Write-Step 'Host prerequisites'
$prerequisites = Test-HostPrerequisites
if ($prerequisites.Problems.Count -gt 0) {
    Write-Host ''
    Write-Host 'This host cannot run the sandbox build yet:' -ForegroundColor Yellow
    foreach ($problem in $prerequisites.Problems) {
        Write-Host "  - $problem" -ForegroundColor Yellow
    }
    if (-not $CheckOnly) {
        throw 'Host prerequisites are not met. See the list above, or re-run with -CheckOnly after fixing them.'
    }
} else {
    Write-Host 'All host prerequisites are met.' -ForegroundColor Green
}

if ($CheckOnly) {
    Write-Host ''
    Write-Host "Cache root    : $CacheRoot"
    Write-Host "Cache present : $(Test-Path -LiteralPath $CacheRoot)"
    return
}

Write-Step "Cache: $CacheRoot"
New-Item -ItemType Directory -Force -Path $downloads | Out-Null

$manifest = [ordered]@{
    generated        = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    repository       = $repositoryRoot
    commit           = (& git -C $repositoryRoot rev-parse HEAD 2>$null)
    cacheRoot        = $CacheRoot
    qtVersion        = $PimioPinned.QtVersion
    loreVersion      = $PimioPinned.LoreVersion
    cmakeVersion     = $PimioPinned.CMakeVersion
    ninjaVersion     = $PimioPinned.NinjaVersion
    nasmVersion      = $PimioPinned.NasmVersion
    perlVersion      = $PimioPinned.PerlVersion
    artifacts        = [ordered]@{}
}

Write-Step 'CMake, Ninja, NASM, MinGit, and Perl'
$cmakeZip = Join-Path $downloads "cmake-$($PimioPinned.CMakeVersion)-windows-x86_64.zip"
$manifest.artifacts['cmake'] = @{
    url    = $PimioPinned.CMakeUrl
    file   = $cmakeZip
    sha256 = (Save-VerifiedFile -Url $PimioPinned.CMakeUrl -Path $cmakeZip -ExpectedSha256 $PimioPinned.CMakeSha256)
    pinned = $true
}
$ninjaZip = Join-Path $downloads "ninja-$($PimioPinned.NinjaVersion)-win.zip"
$manifest.artifacts['ninja'] = @{
    url    = $PimioPinned.NinjaUrl
    file   = $ninjaZip
    sha256 = (Save-VerifiedFile -Url $PimioPinned.NinjaUrl -Path $ninjaZip -ExpectedSha256 $PimioPinned.NinjaSha256)
    pinned = $true
}
$nasmZip = Join-Path $downloads "nasm-$($PimioPinned.NasmVersion)-win64.zip"
$manifest.artifacts['nasm'] = @{
    url    = $PimioPinned.NasmUrl
    file   = $nasmZip
    sha256 = (Save-VerifiedFile -Url $PimioPinned.NasmUrl -Path $nasmZip -ExpectedSha256 $PimioPinned.NasmSha256)
    pinned = $true
}
$minGitZip = Join-Path $downloads "MinGit-$($PimioPinned.MinGitVersion)-64-bit.zip"
$manifest.artifacts['mingit'] = @{
    url    = $PimioPinned.MinGitUrl
    file   = $minGitZip
    sha256 = (Save-VerifiedFile -Url $PimioPinned.MinGitUrl -Path $minGitZip -ExpectedSha256 $PimioPinned.MinGitSha256)
    pinned = $true
}
$perlZip = Join-Path $downloads "strawberry-perl-$($PimioPinned.PerlVersion)-64bit-portable.zip"
$manifest.artifacts['perl'] = @{
    url    = $PimioPinned.PerlUrl
    file   = $perlZip
    sha256 = (Save-VerifiedFile -Url $PimioPinned.PerlUrl -Path $perlZip -ExpectedSha256 $PimioPinned.PerlSha256)
    pinned = $true
}

Write-Step 'Visual Studio Build Tools bootstrapper'
Write-Host '  Microsoft licenses this installer; it is cached for your own use and must never be'
Write-Host '  republished as a pimio release asset. See docs/dependency-bom.md.'
$vsBootstrapper = Join-Path $downloads 'vs_BuildTools.exe'
$manifest.artifacts['vs_buildtools'] = @{
    url        = $PimioPinned.VsBootstrapperUrl
    file       = $vsBootstrapper
    sha256     = (Save-VerifiedFile -Url $PimioPinned.VsBootstrapperUrl -Path $vsBootstrapper)
    pinned     = $false
    components = $PimioPinned.VsComponents
    note       = 'Vendor-controlled bootstrapper: the URL is stable, the bytes are not. The hash above records what this cache actually holds.'
}

Write-Step "LORE $($PimioPinned.LoreVersion)"
$manifest.artifacts['lore'] = [ordered]@{}
foreach ($bundle in $PimioPinned.LoreBundles) {
    $archiveName = Get-PimioLoreArchiveName -Bundle $bundle.Bundle
    $relative = Get-PimioLoreCacheRelativePath -Bundle $bundle.Bundle
    $target = Join-Path $loreCache $relative
    $url = "$($PimioPinned.LoreBaseUrl)/v$($PimioPinned.LoreVersion)/$archiveName"
    $manifest.artifacts['lore'][$bundle.Bundle] = @{
        url    = $url
        file   = $target
        sha256 = (Save-VerifiedFile -Url $url -Path $target -ExpectedSha256 $bundle.Sha256)
        pinned = $true
    }
}

if ($SkipQt) {
    Write-Step 'Qt (skipped)'
    Write-Host '  -SkipQt was given. The sandbox build cannot run without Qt in the cache.'
} else {
    Write-Step "Qt $($PimioPinned.QtVersion)"
    $qtPath = Install-CachedQt -Python $prerequisites.Python
    $manifest.artifacts['qt'] = @{
        version = $PimioPinned.QtVersion
        arch    = $PimioPinned.QtArch
        path    = $qtPath
        source  = $PimioPinned.AqtInstall
    }
}

$manifestPath = Join-Path $CacheRoot 'manifest.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Step 'Ready'
Write-Host "Cache        : $CacheRoot"
Write-Host "Manifest     : $manifestPath"
Write-Host ''
Write-Host 'Next step:'
Write-Host '  tools\local-build\windows\new-sandbox.bat'
Write-Host '  or'
Write-Host '  powershell -ExecutionPolicy Bypass -File tools\local-build\windows\new-sandbox.ps1'
