<#
.SYNOPSIS
    pimio-doctor -- collects one pasteable report about a pimio release archive.

.DESCRIPTION
    Run it from anywhere; it inspects the tree it is shipped in, never the
    system installation. It writes pimio-doctor-report.txt next to itself (or
    to the current directory when the archive is on read-only media) and prints
    the same text to the console.

    It uses only Windows PowerShell 5.1 built-ins. It prints no environment
    variables, so a report is safe to paste into a public issue.

    Exit status: 0 when no hard problem was found, 1 otherwise.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\pimio-doctor.ps1
#>

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Continue'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$appBinary = Join-Path $here 'bin\pimio.exe'
$qtConf = Join-Path $here 'bin\qt.conf'
$platformPluginDir = Join-Path $here 'plugins\platforms'

$report = Join-Path $here 'pimio-doctor-report.txt'
try {
    New-Item -Path $report -ItemType File -Force -ErrorAction Stop | Out-Null
} catch {
    $report = Join-Path (Get-Location).Path 'pimio-doctor-report.txt'
    try {
        New-Item -Path $report -ItemType File -Force -ErrorAction Stop | Out-Null
    } catch {
        Write-Warning 'pimio-doctor: cannot write a report file; printing to the console only'
        $report = $null
    }
}

$problems = New-Object System.Collections.ArrayList

function Add-Problem([string]$Text) {
    [void]$problems.Add($Text)
}

function Say {
    param([string]$Text = '')
    Write-Host $Text
    if ($report) { Add-Content -LiteralPath $report -Value $Text }
}

function Section([string]$Title) {
    Say ''
    Say "== $Title =="
}

function Say-Indented($Lines) {
    foreach ($line in @($Lines)) {
        Say ("  " + $line)
    }
}

Say 'pimio-doctor report'
Say ("generated: " + (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))
Say ("archive root: " + $here)

Section 'System'
try {
    $osInfo = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
    Say-Indented @(
        ("Windows: " + $osInfo.Caption),
        ("version: " + $osInfo.Version),
        ("build: " + $osInfo.BuildNumber)
    )
} catch {
    Say-Indented ("Windows: " + [System.Environment]::OSVersion.VersionString)
}
Say-Indented @(
    ("architecture: " + $env:PROCESSOR_ARCHITECTURE),
    ("PowerShell: " + $PSVersionTable.PSVersion.ToString())
)

Section 'Visual C++ runtime'
$vcRuntime = Join-Path $env:SystemRoot 'System32\vcruntime140.dll'
$vcRuntime1 = Join-Path $env:SystemRoot 'System32\vcruntime140_1.dll'
$vcMissing = $false
foreach ($dll in @($vcRuntime, $vcRuntime1)) {
    if (Test-Path -LiteralPath $dll) {
        Say-Indented ("ok      " + $dll)
    } else {
        Say-Indented ("MISSING " + $dll)
        $vcMissing = $true
    }
}
if ($vcMissing) {
    Add-Problem 'The Microsoft Visual C++ runtime is not installed. Run vc_redist.x64.exe from this archive, then try again.'
}

Section 'Archive layout'
$layoutOk = $true
function Check-Path([string]$Path, [string]$Label) {
    if (Test-Path -LiteralPath $Path) {
        Say-Indented ("ok      " + $Label)
    } else {
        Say-Indented ("MISSING " + $Label)
        $script:layoutOk = $false
    }
}
Check-Path $appBinary 'bin\pimio.exe'
Check-Path $qtConf 'bin\qt.conf'
Check-Path (Join-Path $here 'bin\Qt6Core.dll') 'bin\Qt6Core.dll'
Check-Path $platformPluginDir 'plugins\platforms'
Check-Path (Join-Path $platformPluginDir 'qwindows.dll') 'plugins\platforms\qwindows.dll'
Check-Path (Join-Path $here 'qml') 'qml\'
Check-Path (Join-Path $here 'qml\QtQuick') 'qml\QtQuick'
if (-not $layoutOk) {
    Add-Problem 'The archive layout is incomplete. Re-extract the archive without moving or renaming anything inside it, and do not copy individual files out of the tree.'
}

Section 'qt.conf'
if (Test-Path -LiteralPath $qtConf) {
    Say-Indented ("file: " + $qtConf)
    $qtConfLines = Get-Content -LiteralPath $qtConf
    Say-Indented $qtConfLines
    $prefixLine = $qtConfLines | Where-Object { $_ -match '^\s*Prefix\s*=' } | Select-Object -First 1
    if ($prefixLine) {
        $prefix = ($prefixLine -replace '^\s*Prefix\s*=\s*', '').Trim()
        $resolved = $null
        try {
            $resolved = (Resolve-Path -LiteralPath (Join-Path (Split-Path -Parent $qtConf) $prefix) -ErrorAction Stop).Path
        } catch {
            $resolved = $null
        }
        if ($resolved) {
            Say-Indented ("Prefix -> " + $resolved)
            if ($resolved.TrimEnd('\') -ne $here.TrimEnd('\')) {
                Add-Problem ("qt.conf Prefix resolves to $resolved, which is not the archive root $here. The archive has the wrong directory layout; Qt will look for plugins outside the extracted tree.")
            }
        } else {
            Say-Indented ("Prefix -> <does not exist: " + $prefix + ">")
            Add-Problem 'qt.conf Prefix points at a directory that does not exist.'
        }
    } else {
        Say-Indented '<no Prefix entry>'
    }
} else {
    Say-Indented '(no qt.conf found)'
}

Section 'Platform plugins present'
if (Test-Path -LiteralPath $platformPluginDir) {
    Say-Indented (Get-ChildItem -LiteralPath $platformPluginDir | ForEach-Object { $_.Name })
} else {
    Say-Indented ("(none: " + $platformPluginDir + " does not exist)")
}

Section 'Bundled DLLs'
$bundledDlls = @()
if (Test-Path -LiteralPath (Join-Path $here 'bin')) {
    $bundledDlls = Get-ChildItem -LiteralPath (Join-Path $here 'bin') -Filter *.dll |
        ForEach-Object { $_.Name }
    Say-Indented $bundledDlls
} else {
    Say-Indented '(no bin directory)'
}

$requiredDlls = @(
    'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Qml.dll', 'Qt6Quick.dll',
    'Qt6QuickControls2.dll', 'Qt6Network.dll', 'Qt6OpenGL.dll'
)
$missingRequired = $requiredDlls | Where-Object { $bundledDlls -notcontains $_ }
if ($missingRequired) {
    Say-Indented ("MISSING required: " + ($missingRequired -join ', '))
    Add-Problem ("These Qt libraries are missing from bin\: " + ($missingRequired -join ', ') + ". The archive is incomplete.")
}

Section 'DLL dependency walk'
$dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
if ($dumpbin -and (Test-Path -LiteralPath $appBinary)) {
    Say-Indented (& $dumpbin.Source /dependents $appBinary 2>&1)
} else {
    Say-Indented '(dumpbin is not available; the bundled DLL inventory above is the substitute)'
}

Section 'Application launch (QT_DEBUG_PLUGINS=1)'
if (Test-Path -LiteralPath $appBinary) {
    $previousDebug = $env:QT_DEBUG_PLUGINS
    $previousRules = $env:QT_LOGGING_RULES
    $env:QT_DEBUG_PLUGINS = '1'
    $env:QT_LOGGING_RULES = 'qt.qpa.*=true'
    $stdout = [System.IO.Path]::GetTempFileName()
    $stderr = [System.IO.Path]::GetTempFileName()
    $launchStatus = 1
    try {
        $process = Start-Process -FilePath $appBinary -ArgumentList '--version' `
            -WorkingDirectory $here -NoNewWindow -PassThru `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        if (-not $process.WaitForExit(60000)) {
            $process.Kill()
            Add-Problem 'The application did not exit within 60 seconds.'
        }
        $launchStatus = $process.ExitCode
    } catch {
        Say-Indented ("failed to start: " + $_.Exception.Message)
        Add-Problem ("The application could not be started: " + $_.Exception.Message)
    }
    $launchOutput = @()
    foreach ($file in @($stdout, $stderr)) {
        if (Test-Path -LiteralPath $file) {
            $launchOutput += Get-Content -LiteralPath $file
            Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue
        }
    }
    $env:QT_DEBUG_PLUGINS = $previousDebug
    $env:QT_LOGGING_RULES = $previousRules
    Say-Indented $launchOutput
    Say-Indented ("exit status: " + $launchStatus)
    if ($launchStatus -ne 0) {
        $joined = ($launchOutput -join "`n")
        if ($joined -match 'no Qt platform plugin could be initialized') {
            Add-Problem 'Qt could not initialize a platform plugin. The plugin search path and any load error appear in the launch output above.'
        } elseif ($joined -match 'is not installed') {
            Add-Problem 'A QML module is missing from this build. The archive was produced without the QML import tree.'
        } else {
            Add-Problem ("The application exited with status $launchStatus. See the launch output above.")
        }
    }
} else {
    Say-Indented '(application binary not found; skipping)'
}

Section 'LIKELY CAUSE'
if ($problems.Count -eq 0) {
    Say-Indented @(
        'No hard problem detected. The archive looks complete and the',
        'application started successfully.'
    )
    $exitCode = 0
} else {
    foreach ($problem in $problems) {
        Say-Indented ("- " + $problem)
    }
    $exitCode = 1
}

Say ''
if ($report) { Say ("Report written to: " + $report) }
exit $exitCode
