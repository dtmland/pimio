# Pinned toolchain for the Windows local build environment.
#
# Dot-source this file; it defines $PimioPinned, $PimioSandboxPaths, and the
# helpers below.
#
# The point of the local environment is that a build made on a developer's
# machine is comparable with a build made by CI, so Qt and LORE are pinned to
# the same shared inputs as CI: tools/build/qt.env and cmake/PimioLore.cmake.
# Only Windows-specific provisioning belongs here, not copies of product pins.

Set-StrictMode -Version Latest

$PimioPinned = @{
    # aqtinstall's identifier for the MSVC 2022 64-bit desktop build, and the
    # directory name it installs into.
    QtArch     = 'win64_msvc2022_64'
    QtHostDir  = 'msvc2022_64'
    # Portable tools. These are extracted, never installed, so the sandbox needs
    # no installer for them and their versions cannot drift with the host.
    CMakeVersion = '3.31.6'
    CMakeUrl     = 'https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-windows-x86_64.zip'
    CMakeSha256  = 'd163cd3ab4959b0a53fa8988f2ddbd2e6c501658201e6a154386bad9dbe4f836'
    NinjaVersion = '1.12.1'
    NinjaUrl     = 'https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip'
    NinjaSha256  = 'f550fec705b6d6ff58f2db3c374c2277a37691678d6aba463adcbb129108467a'
    NasmVersion  = '2.16.03'
    NasmUrl      = 'https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/win64/nasm-2.16.03-win64.zip'
    NasmSha256   = '3ee4782247bcb874378d02f7eab4e294a84d3d15f3f6ee2de2f47a46aa7226e6'

    # MinGit: the minimal Git for Windows distribution, needed in the sandbox
    # because libavif's aom codec is fetched via git during cmake configure.
    MinGitVersion = '2.47.0.2'
    MinGitUrl     = 'https://github.com/git-for-windows/git/releases/download/v2.47.0.windows.2/MinGit-2.47.0.2-64-bit.zip'
    MinGitSha256  = 'c4a5d3a2adda98b25fe59349733fca56b3843360b962dba5535282a9d8120b31'

    # Strawberry Perl portable: required by libaom (the AV1 codec used by libavif)
    # at CMake configure time to generate its assembly sources. GitHub-hosted CI
    # runners come with Strawberry Perl pre-installed; the sandbox image does not.
    # Hosted at https://github.com/shogo82148/strawberry-perl-releases; SHA-256
    # verified against the shogo82148/actions-setup-perl versions manifest.
    PerlVersion = '5.38.2.2'
    PerlUrl     = 'https://github.com/shogo82148/strawberry-perl-releases/releases/download/5.38.2.2/strawberry-perl-5.38.2.2-64bit-portable.zip'
    PerlSha256  = 'ea451686065d6338d7e4d4a04c9af49f17951d15aa4c2e19ab8cb56fa2373440'

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
        Reloads the authoritative inputs, failing before downloads if malformed.
    #>
    param(
        [Parameter(Mandatory = $true)][string] $RepositoryRoot
    )

    $sharedPins = Get-PimioBuildPins -RepositoryRoot $RepositoryRoot
    foreach ($entry in $sharedPins.GetEnumerator()) {
        $PimioPinned[$entry.Key] = $entry.Value
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

. (Join-Path $PSScriptRoot '../../build/pins.ps1')
Assert-PimioPinsMatchRepository -RepositoryRoot (Get-PimioRepositoryRoot)
