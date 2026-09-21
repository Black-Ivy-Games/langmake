[CmdletBinding()]
param(
    [ValidateRange(1, 8)]
    [int]$Jobs = 8,
    [string]$BuildDirectory = 'build'
)

$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildDirectory = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
$workspacePrefix = $projectRoot.TrimEnd([char[]]@('\', '/')) + [System.IO.Path]::DirectorySeparatorChar
if (-not $buildDirectory.StartsWith($workspacePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'BuildDirectory must be inside the Langmake workspace.'
}
$cacheDirectory = Join-Path $projectRoot '.cache'
$temporaryDirectory = Join-Path $cacheDirectory 'tmp'

foreach ($directory in @($buildDirectory, $cacheDirectory, $temporaryDirectory)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

# All process-local temporary files and supported caches stay in this project.
$env:TEMP = $temporaryDirectory
$env:TMP = $temporaryDirectory
$env:TMPDIR = $temporaryDirectory
$env:XDG_CACHE_HOME = $cacheDirectory
$env:CCACHE_DIR = Join-Path $cacheDirectory 'ccache'
$env:PIP_CACHE_DIR = Join-Path $cacheDirectory 'pip'
$env:PYTHONPYCACHEPREFIX = Join-Path $cacheDirectory 'python'

foreach ($tool in @('cmake', 'ninja', 'g++', 'git')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Required tool '$tool' was not found on PATH. No tools were installed."
    }
}

Write-Host 'Configuring the Windows Release build...'
& cmake -S $projectRoot -B $buildDirectory -G Ninja '-DCMAKE_BUILD_TYPE=Release' "-DFETCHCONTENT_BASE_DIR=$(Join-Path $projectRoot '.deps')"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE."
}

Write-Host "Building with up to $Jobs jobs..."
& cmake --build $buildDirectory --parallel $Jobs
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE."
}

$executable = Join-Path $buildDirectory 'langmake.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "The build completed but the expected executable is missing: $executable"
}

Write-Host "Built: $executable"
# Keep `new` independent of the process working directory and build location.
# Copy authored references only; runtime state and caches are never templates.
foreach ($template in @('2d', '3d', 'rockets-2d', 'rockets-3d', 'adventure', 'strategy', 'shared-2d', 'workflow-2d')) {
    $source = Join-Path $projectRoot "examples/$template"
    $sourcePrefix = [System.IO.Path]::GetFullPath($source).TrimEnd([char[]]@('\', '/')) + [System.IO.Path]::DirectorySeparatorChar
    foreach ($file in (Get-ChildItem -LiteralPath $source -File -Recurse -Force)) {
        $relative = $file.FullName.Substring($sourcePrefix.Length)
        if ($relative -match '(^|[\\/])(\.cache|\.langmake)([\\/]|$)') { continue }
        $target = Join-Path $buildDirectory (Join-Path "examples/$template" $relative)
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target -Force
    }
}
Write-Host "All eight templates are available beside the executable. Verify with scripts/test.ps1 -BuildDirectory '$BuildDirectory'."
