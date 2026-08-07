# Pinned toolchain for the Windows local build environment.
#
# Dot-source this file; it defines $PimioPinned, $PimioSandboxPaths, and the
# helpers below.
#
# The point of the local environment is that a build made on a developer's
# machine is comparable with a build made by CI, so Qt and LORE are pinned to
# the same versions as .github/workflows/ci.yml and cmake/PimioLore.cmake. Those
# two files remain authoritative: Assert-PimioPinsMatchRepository re-reads them
# and refuses to run once this file has drifted, because a silently different Qt
# or LORE would make every local result incomparable with CI.

Set-StrictMode -Version Latest

$PimioPinned = @{
    # Must match PIMIO_QT_VERSION in .github/workflows/ci.yml.
    QtVersion  = '6.8.3'
    # aqtinstall's identifier for the MSVC 2022 64-bit desktop build, and the
    # directory name it installs into.
    QtArch     = 'win64_msvc2022_64'
    QtHostDir  = 'msvc2022_64'
    # Must match the modules installed by .github/workflows/ci.yml. The Qt base
    # package carries qtbase, qtdeclarative and qtshadertools, but pimio also
    # links Qt6::Multimedia (see src/thumbnail/CMakeLists.txt) and decodes the
    # extra image formats, so those add-on modules must be installed too or
    # configuration fails with "Failed to find required Qt component Multimedia".
    QtModules  = @('qtmultimedia', 'qtimageformats')
    # aqtinstall is part of the toolchain, so it is pinned like the rest of it.
    AqtInstall = 'aqtinstall==3.3.0'

    # Must match PIMIO_LORE_VERSION in cmake/PimioLore.cmake. The checksums are
    # the recorded Windows entries from that same file.
    LoreVersion = '0.8.5'
    LoreTriple  = 'x86_64-pc-windows-msvc'
    LoreBaseUrl = 'https://github.com/EpicGames/lore/releases/download'
    LoreBundles = @(
        @{ Bundle = 'liblore'; Sha256 = '4beb1500db6b3fde2f0107378ca61d609f3aa4c18c8adfe57bfe389d70155b81' }
        @{ Bundle = 'lore';    Sha256 = 'c213169d251b73feb3fdf1655b9b5e6717a6a862762825918cc318a570018ded' }
    )

    # Portable tools. Both are extracted, never installed, so the sandbox needs
    # no installer for them and their versions cannot drift with the host.
    CMakeVersion = '3.31.6'
    CMakeUrl     = 'https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-windows-x86_64.zip'
    CMakeSha256  = 'd163cd3ab4959b0a53fa8988f2ddbd2e6c501658201e6a154386bad9dbe4f836'
    NinjaVersion = '1.12.1'
    NinjaUrl     = 'https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip'
    NinjaSha256  = 'f550fec705b6d6ff58f2db3c374c2277a37691678d6aba463adcbb129108467a'

    # The Visual Studio Build Tools bootstrapper is deliberately not hash-pinned:
    # Microsoft republishes this one URL for every servicing update, so a
    # recorded hash would fail within weeks. prepare.ps1 records the hash it
    # actually downloaded in the cache manifest instead, which is what lets a
    # local build be identified after the fact.
    VsBootstrapperUrl = 'https://aka.ms/vs/17/release/vs_BuildTools.exe'
    VsComponents      = @(
        'Microsoft.VisualStudio.Workload.VCTools'
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
        'Microsoft.VisualStudio.Component.Windows11SDK.22621'
    )
}

# Paths inside the sandbox. They are fixed so the generated .wsb file, the
# bootstrap script, and the README all describe the same machine.
$PimioSandboxPaths = @{
    Source  = 'C:\pimio\source'
    Cache   = 'C:\pimio\cache'
    Results = 'C:\pimio\results'
    Work    = 'C:\pimio\work'
    Tools   = 'C:\pimio\tools'
}

function Get-PimioRepositoryRoot {
    <#
    .SYNOPSIS
        Returns the repository root containing this script.
    #>
    return (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
}

function Get-PimioDefaultCacheRoot {
    <#
    .SYNOPSIS
        Returns the default host tool cache for a checkout.
    .DESCRIPTION
        .cache/ is already git-ignored, so the tool cache sits next to the
        checkout it serves and can never be committed.
    #>
    param(
        [Parameter(Mandatory = $true)][string] $RepositoryRoot
    )
    return (Join-Path $RepositoryRoot '.cache\local-build\windows')
}

function Assert-PimioPinsMatchRepository {
    <#
    .SYNOPSIS
        Fails when the pins in this file disagree with the authoritative ones.
    #>
    param(
        [Parameter(Mandatory = $true)][string] $RepositoryRoot
    )

    $workflow = Join-Path $RepositoryRoot '.github\workflows\ci.yml'
    $loreModule = Join-Path $RepositoryRoot 'cmake\PimioLore.cmake'

    foreach ($file in @($workflow, $loreModule)) {
        if (-not (Test-Path -LiteralPath $file)) {
            throw "Cannot verify the pinned versions: $file is missing. Run this from a pimio checkout."
        }
    }

    $ciQtMatch = Select-String -LiteralPath $workflow -Pattern 'PIMIO_QT_VERSION:\s*([0-9.]+)' |
        Select-Object -First 1
    if (-not $ciQtMatch) {
        throw "Cannot find PIMIO_QT_VERSION in $workflow."
    }
    $ciQt = $ciQtMatch.Matches[0].Groups[1].Value
    if ($ciQt -ne $PimioPinned.QtVersion) {
        throw "Qt pin drift: ci.yml pins Qt $ciQt, pinned.ps1 pins $($PimioPinned.QtVersion). Update pinned.ps1."
    }

    $ciModulesMatch = Select-String -LiteralPath $workflow -Pattern 'modules:\s*(.+)$' |
        Select-Object -First 1
    if (-not $ciModulesMatch) {
        throw "Cannot find the Qt 'modules:' line in $workflow."
    }
    $ciModules = ($ciModulesMatch.Matches[0].Groups[1].Value.Trim() -split '\s+') | Sort-Object
    $localModules = @($PimioPinned.QtModules) | Sort-Object
    if (($ciModules -join ' ') -ne ($localModules -join ' ')) {
        throw "Qt module pin drift: ci.yml installs '$($ciModules -join ' ')', pinned.ps1 installs '$($localModules -join ' ')'. Update pinned.ps1."
    }

    $loreMatch = Select-String -LiteralPath $loreModule -Pattern 'PIMIO_LORE_VERSION\s+"([0-9.]+)"' |
        Select-Object -First 1
    if (-not $loreMatch) {
        throw "Cannot find PIMIO_LORE_VERSION in $loreModule."
    }
    $loreVersion = $loreMatch.Matches[0].Groups[1].Value
    if ($loreVersion -ne $PimioPinned.LoreVersion) {
        throw "LORE pin drift: PimioLore.cmake pins LORE $loreVersion, pinned.ps1 pins $($PimioPinned.LoreVersion). Update pinned.ps1."
    }

    $loreText = Get-Content -LiteralPath $loreModule -Raw
    foreach ($bundle in $PimioPinned.LoreBundles) {
        $expected = "$($bundle.Bundle)|$($PimioPinned.LoreTriple)|zip|$($bundle.Sha256)"
        if (-not $loreText.Contains($expected)) {
            throw "LORE checksum drift for $($bundle.Bundle): PimioLore.cmake does not record $($bundle.Sha256). Update pinned.ps1."
        }
    }
}

function Get-PimioLoreArchiveName {
    param(
        [Parameter(Mandatory = $true)][string] $Bundle
    )
    return "$Bundle-v$($PimioPinned.LoreVersion)-$($PimioPinned.LoreTriple).zip"
}

function Get-PimioLoreCacheRelativePath {
    <#
    .SYNOPSIS
        Returns the archive path cmake/PimioLore.cmake expects, relative to the
        LORE cache directory. Laying the cache out this way is what lets the
        sandbox build reuse the download instead of fetching it again.
    #>
    param(
        [Parameter(Mandatory = $true)][string] $Bundle
    )
    $archive = Get-PimioLoreArchiveName -Bundle $Bundle
    return "v$($PimioPinned.LoreVersion)\$($PimioPinned.LoreTriple)\$Bundle\$archive"
}
