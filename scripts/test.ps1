[CmdletBinding()]
param(
    [switch]$Graphics,
    [string]$BuildDirectory = 'build'
)

$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildDirectory = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
$workspacePrefix = $projectRoot.TrimEnd([char[]]@('\', '/')) + [System.IO.Path]::DirectorySeparatorChar
if (-not $buildDirectory.StartsWith($workspacePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'BuildDirectory must be inside the Langmake workspace.'
}
$executable = Join-Path $buildDirectory 'langmake.exe'
$cacheDirectory = Join-Path $projectRoot '.cache'
$temporaryDirectory = Join-Path $cacheDirectory 'tmp'
New-Item -ItemType Directory -Path $temporaryDirectory -Force | Out-Null
$env:TEMP = $temporaryDirectory
$env:TMP = $temporaryDirectory
$env:TMPDIR = $temporaryDirectory
$env:XDG_CACHE_HOME = $cacheDirectory
$env:PYTHONPYCACHEPREFIX = Join-Path $cacheDirectory 'python'

if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw 'Build first: powershell -ExecutionPolicy Bypass -File scripts/build.ps1'
}
foreach ($tool in @('ctest', 'python')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Required tool '$tool' was not found on PATH."
    }
}

& ctest --test-dir $buildDirectory --output-on-failure --no-tests=error
if ($LASTEXITCODE -ne 0) {
    throw "CTest failed with exit code $LASTEXITCODE."
}

$integrationTests = Join-Path $PSScriptRoot 'integration_tests.py'
$playthroughTests = Join-Path $PSScriptRoot 'playthrough_tests.py'
$liveTests = Join-Path $PSScriptRoot 'live_tests.py'
$assetTests = Join-Path $PSScriptRoot 'asset_tests.py'
$workflowTests = Join-Path $PSScriptRoot 'workflow_tests.py'
$referenceTests = Join-Path $PSScriptRoot 'create_gameplay_examples.py'
$testFiles = @($integrationTests, $playthroughTests, $assetTests, $referenceTests)
if ($Graphics) { $testFiles += @($liveTests, $workflowTests) }
foreach ($testFile in $testFiles) {
    if (-not (Test-Path -LiteralPath $testFile -PathType Leaf)) {
        throw "Test entry point is missing: $testFile"
    }
}

Push-Location $projectRoot
try {
    foreach ($relativeTest in @('music/test_music.py','ios/test_ios.py','android/test_android.py','test_mobile_assets.py','windows/test_windows.py','workflow/test_verify.py','workflow/test_operations.py')) {
        & python (Join-Path $PSScriptRoot $relativeTest)
        if ($LASTEXITCODE -ne 0) { throw "Authoring tools test failed: $relativeTest" }
    }
    $authoringArguments = @((Join-Path $PSScriptRoot 'workflow/integration_tests.py'),'--engine',$executable)
    if ($Graphics) { $authoringArguments += '--graphics' }
    & python @authoringArguments
    if ($LASTEXITCODE -ne 0) { throw 'Per-game workflow integration tests failed.' }
    & python (Join-Path $PSScriptRoot 'music/integration_tests.py') --engine $executable
    if ($LASTEXITCODE -ne 0) { throw 'Recorded music source/authoring integration tests failed.' }
    $integrationArguments = @($integrationTests, '--engine', $executable)
    if ($Graphics) { $integrationArguments += '--graphics' }
    & python @integrationArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Integration tests failed with exit code $LASTEXITCODE."
    }
    & python $playthroughTests --engine $executable
    if ($LASTEXITCODE -ne 0) {
        throw "Reference-game playthrough tests failed with exit code $LASTEXITCODE."
    }
    & python (Join-Path $PSScriptRoot 'cook_tests.py') --engine $executable
    if ($LASTEXITCODE -ne 0) { throw "Player content bundle tests failed with exit code $LASTEXITCODE." }
    & python (Join-Path $PSScriptRoot 'scene_catalog_tests.py') --engine $executable
    if ($LASTEXITCODE -ne 0) { throw 'Scene-local catalog tests failed.' }
    $assetArguments = @($assetTests, '--engine', $executable)
    if ($Graphics) { $assetArguments += '--graphics' }
    & python @assetArguments
    if ($LASTEXITCODE -ne 0) { throw "Asset tests failed with exit code $LASTEXITCODE." }
    $referenceArguments = @($referenceTests, '--verify', '--engine', $executable)
    if ($Graphics) { $referenceArguments += '--graphics' }
    & python @referenceArguments
    if ($LASTEXITCODE -ne 0) { throw "Adventure, strategy and shared-2d reference checks failed with exit code $LASTEXITCODE." }
    if ($Graphics) {
        & (Join-Path $buildDirectory 'preview_session_tests.exe') $projectRoot $buildDirectory
        if ($LASTEXITCODE -ne 0) {
            throw "Callback preview lifecycle tests failed with exit code $LASTEXITCODE."
        }
        & python $liveTests --engine $executable
        if ($LASTEXITCODE -ne 0) {
            throw "Live preview tests failed with exit code $LASTEXITCODE."
        }
        & python $workflowTests --engine $executable
        if ($LASTEXITCODE -ne 0) {
            throw "Agent workflow regression tests failed with exit code $LASTEXITCODE."
        }
    }
}
finally {
    Pop-Location
}

Write-Host 'Native, integration and reference-game playthrough tests passed.'
if ($Graphics) { Write-Host 'Graphics capture, live inspection, player isolation and HUD workflow tests passed.' }
