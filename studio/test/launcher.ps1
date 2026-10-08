[CmdletBinding()]
param([string]$PackageDirectory = 'dist/studio2/Langmake-Studio-0.5.0')
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$fixture = 'studio/.cache/launcher-' + [Guid]::NewGuid().ToString()
$global:studioLauncherTestProcess = $null
$priorData = $env:LANGMAKE_STUDIO_DATA
$priorEngine = $env:LANGMAKE_ENGINE
# Isolated fixture data permits this test alongside the human's Studio.
# Only bypass the launcher's coarse older-version process guard; launch for real.
function Get-Process { [CmdletBinding()]param([string[]]$Name) @() }
function Start-Process {
    [CmdletBinding()]param($FilePath,$WorkingDirectory,$WindowStyle,[switch]$PassThru)
    if ($WindowStyle -ne 'Normal' -or !$PassThru) { throw 'Interactive launch must be visible and observed.' }
    $global:studioLauncherTestProcess = Microsoft.PowerShell.Management\Start-Process -FilePath $FilePath -WorkingDirectory $WorkingDirectory -WindowStyle $WindowStyle -PassThru
    return $global:studioLauncherTestProcess
}
try {
    & (Join-Path $workspace 'studio/open-packaged.ps1') -PackageDirectory $PackageDirectory -DataDirectory $fixture
    $started = $global:studioLauncherTestProcess
    $started.Refresh()
    if ($started.MainWindowHandle -eq [IntPtr]::Zero -or $started.MainWindowTitle -ne 'Langmake Studio') { throw 'No visible Studio window.' }
    if ($env:LANGMAKE_STUDIO_DATA -ne $priorData -or $env:LANGMAKE_ENGINE -ne $priorEngine) { throw 'Launcher environment was not restored.' }
    @{ok=$true;realLaunch=$true;visibleWindow=$true;title=$started.MainWindowTitle;fixture=$fixture;package=$PackageDirectory;checks=@('real packaged process has a visible main window','launcher waits for window','environment restored','private fixture data')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $workspace "$fixture/launcher-evidence.json")
    Write-Output "Launcher visibility passed. Evidence: $fixture/launcher-evidence.json"
} finally {
    if ($global:studioLauncherTestProcess -and !$global:studioLauncherTestProcess.HasExited) {
        [void]$global:studioLauncherTestProcess.CloseMainWindow()
        if (!$global:studioLauncherTestProcess.WaitForExit(10000)) { $global:studioLauncherTestProcess.Kill() }
    }
    Remove-Variable -Scope Global -Name studioLauncherTestProcess -ErrorAction SilentlyContinue
}
