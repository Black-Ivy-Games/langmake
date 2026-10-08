[CmdletBinding()]
param(
    [string]$PackageDirectory = 'dist/studio2/Langmake-Studio-0.5.0',
    [string]$DataDirectory = ''
)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function WorkspacePath([string]$Value) {
    $resolved = if ([IO.Path]::IsPathRooted($Value)) { [IO.Path]::GetFullPath($Value) } else { [IO.Path]::GetFullPath((Join-Path $workspace $Value)) }
    if (!$resolved.StartsWith($workspace.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Package and data directories must be inside this workspace.'
    }
    return $resolved
}
$package = WorkspacePath $PackageDirectory
$executable = Join-Path $package 'Langmake Studio.exe'
$runtime = Join-Path $package 'runtime/langmake.exe'
if (!(Test-Path -LiteralPath $executable) -or !(Test-Path -LiteralPath $runtime)) { throw 'The selected Studio package is incomplete.' }
# Older releases do not take a single-instance lock. Never share their live data.
if (Get-Process -Name 'Langmake Studio' -ErrorAction SilentlyContinue) {
    throw 'Close the existing Langmake Studio window first, then run this launcher again. Your conversations and provider settings will be reused.'
}
if (!$DataDirectory) {
    $previous = 'dist/Langmake-Studio-0.5.0/.data'
    $DataDirectory = if (Test-Path -LiteralPath (Join-Path $workspace "$previous/studio.json")) { $previous } else { "$PackageDirectory/.data" }
}
$data = WorkspacePath $DataDirectory
$previousData = $env:LANGMAKE_STUDIO_DATA
$previousEngine = $env:LANGMAKE_ENGINE
$previousNodeMode = $env:ELECTRON_RUN_AS_NODE
try {
    $env:LANGMAKE_STUDIO_DATA = $data
    $env:LANGMAKE_ENGINE = $runtime
    Remove-Item Env:ELECTRON_RUN_AS_NODE -ErrorAction SilentlyContinue
    # This is the human-facing application, not a background helper.
    $launched = Start-Process -FilePath $executable -WorkingDirectory $package -WindowStyle Normal -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $launched.Refresh()
        if ($launched.HasExited) { throw "Studio exited during startup (code $($launched.ExitCode))." }
        if ($launched.MainWindowHandle -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($launched.MainWindowHandle -eq [IntPtr]::Zero) {
        throw "Studio started (PID $($launched.Id)), but no visible window appeared within 15 seconds."
    }
    Write-Output "Opened Studio build studio2. Data: $data"
} finally {
    $env:LANGMAKE_STUDIO_DATA = $previousData
    $env:LANGMAKE_ENGINE = $previousEngine
    $env:ELECTRON_RUN_AS_NODE = $previousNodeMode
}
