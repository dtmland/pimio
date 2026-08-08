# Open the pimio Field Notes manual-test checklist in the default Windows browser.
#
# Usage (from the repository root or from within the Windows Sandbox):
#   tools\local-build\windows\open-field-notes.ps1 [-BuildDir <path>]
#
# Options:
#   -BuildDir <path>  Path to the staged build output (shown in the console
#                     message for reference).  Defaults to the value of the
#                     PIMIO_STAGE_DIR environment variable, or empty.
#
# The shared HTML lives at tools/manual-test/field-notes.html; this wrapper
# resolves the path relative to the repository root and opens it via
# Start-Process so Windows picks the default browser.

[CmdletBinding()]
param(
    [string]$BuildDir = $env:PIMIO_STAGE_DIR
)

$ErrorActionPreference = 'Stop'

$repoRoot    = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..'))
$fieldNotes  = Join-Path $repoRoot 'tools\manual-test\field-notes.html'

if (-not (Test-Path -LiteralPath $fieldNotes)) {
    Write-Error "Field Notes not found: $fieldNotes"
    exit 1
}

Start-Process $fieldNotes
Write-Host "Field Notes opened in the browser: $fieldNotes"

if ($BuildDir) {
    Write-Host "Staged application: $BuildDir"
}
