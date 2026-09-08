# Bootstrap reader for Windows PowerShell 5.1: no CMake/Python installation needed.
Set-StrictMode -Version Latest

function Get-PimioPinValue {
    param(
        [string] $Text,
        [string] $Pattern,
        [string] $Name
    )
    $found = [regex]::Matches($Text, $Pattern)
    if ($found.Count -ne 1) {
        throw "Expected exactly one $Name in the shared build inputs; found $($found.Count)."
    }
    return $found[0].Groups[1].Value
}

function Get-PimioBuildPins {
    param([Parameter(Mandatory = $true)][string] $RepositoryRoot)

    $qt = Get-Content -LiteralPath (Join-Path $RepositoryRoot 'tools/build/qt.env') -Raw -ErrorAction Stop
    $lore = Get-Content -LiteralPath (Join-Path $RepositoryRoot 'cmake/PimioLore.cmake') -Raw -ErrorAction Stop
    $version = Get-PimioPinValue $lore '(?m)^set\(PIMIO_LORE_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"' 'LORE version'
    $baseUrl = Get-PimioPinValue $lore '(?m)^set\(PIMIO_LORE_BASE_URL "(https://[^"\s]+)"' 'LORE base URL'
    $tablePattern = '(?ms)^set\(_pimio_lore_checksums_' + [regex]::Escape($version) + '\s+(.*?)^\)'
    $table = Get-PimioPinValue $lore $tablePattern "LORE $version checksum table"
    $triple = 'x86_64-pc-windows-msvc'
    $bundles = foreach ($bundle in @('liblore', 'lore')) {
        $pattern = '"' + $bundle + '\|' + $triple + '\|zip\|([0-9a-f]{64})"'
        @{ Bundle = $bundle; Sha256 = (Get-PimioPinValue $table $pattern "$bundle Windows checksum") }
    }

    return @{
        QtVersion = (Get-PimioPinValue $qt '(?m)^PIMIO_QT_VERSION=([0-9]+\.[0-9]+\.[0-9]+)\r?$' 'Qt version')
        QtModules = ((Get-PimioPinValue $qt '(?m)^PIMIO_QT_MODULES="([a-z0-9]+(?: [a-z0-9]+)*)"\r?$' 'Qt modules') -split ' ')
        AqtInstall = 'aqtinstall==' + (Get-PimioPinValue $qt '(?m)^PIMIO_AQTINSTALL_VERSION=([0-9]+\.[0-9]+\.[0-9]+)\r?$' 'aqtinstall version')
        LoreVersion = $version
        LoreBaseUrl = $baseUrl
        LoreTriple = $triple
        LoreBundles = @($bundles)
    }
}
