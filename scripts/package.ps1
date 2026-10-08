[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build',
    [string]$AndroidPlayerDirectory = '.cache/workflow-qualification/android-player-0.5.0-final',
    [string]$PackageDirectory = 'dist/Langmake-0.5.0',
    [switch]$IncludeMusicAuthoring,
    [string]$MusicExampleDirectory = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildDirectory = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
$workspacePrefix = $projectRoot.TrimEnd([char[]]@('\', '/')) + [System.IO.Path]::DirectorySeparatorChar
if (-not $buildDirectory.StartsWith($workspacePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'BuildDirectory must be inside the Langmake workspace.'
}
$executable = Join-Path $buildDirectory 'langmake.exe'
$destination = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $PackageDirectory))
$distributionPrefix = (Join-Path $projectRoot 'dist').TrimEnd([char[]]@('\', '/')) + [System.IO.Path]::DirectorySeparatorChar
if (-not $destination.StartsWith($distributionPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'PackageDirectory must be a named package directory inside workspace/dist.'
}
$templates = @('2d', '3d', 'rockets-2d', 'rockets-3d', 'adventure', 'strategy', 'shared-2d', 'workflow-2d')
$cacheDirectory = Join-Path $projectRoot '.cache'
$temporaryDirectory = Join-Path $cacheDirectory 'tmp'
New-Item -ItemType Directory -Path $temporaryDirectory -Force | Out-Null
$env:TEMP = $temporaryDirectory
$env:TMP = $temporaryDirectory
$env:TMPDIR = $temporaryDirectory
$env:XDG_CACHE_HOME = $cacheDirectory
$env:PYTHONPYCACHEPREFIX = Join-Path $cacheDirectory 'python'

foreach ($requiredFile in @($executable, (Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md'), (Join-Path $projectRoot 'dependencies.lock.json'), (Join-Path $buildDirectory 'shaders/vs_cubes.bin'), (Join-Path $buildDirectory 'shaders/fs_cubes.bin'), (Join-Path $buildDirectory 'fonts/LatoLatin-Regular.ttf'))) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required package input is missing: $requiredFile. Build the engine and prepare its license inventory first."
    }
}
foreach ($relativeDirectory in @($templates | ForEach-Object { "examples/$_" }) + @('licenses', 'prompts')) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $relativeDirectory) -PathType Container)) {
        throw "Required package directory is missing: $relativeDirectory"
    }
}
$inputCapabilities = (& $executable capabilities) -join "`n"
if ($LASTEXITCODE -ne 0 -or ($inputCapabilities | ConvertFrom-Json).version -ne '0.5.0') {
    throw 'Packaging requires the current Langmake 0.5.0 executable.'
}
$dependencyLock = Get-Content -LiteralPath (Join-Path $projectRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
if ($dependencyLock.release -ne '0.5.0') { throw 'The dependency inventory must describe release 0.5.0.' }
foreach ($license in $dependencyLock.license_files) {
    $licensePath = Join-Path $projectRoot $license.path
    if (-not (Test-Path -LiteralPath $licensePath -PathType Leaf) -or (Get-FileHash -LiteralPath $licensePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $license.sha256) {
        throw "Missing or changed license inventory entry: $($license.path)"
    }
}
foreach ($asset in $dependencyLock.bundled_upstream_assets) {
    $assetPath = Join-Path $buildDirectory $asset.path
    if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf) -or (Get-FileHash -LiteralPath $assetPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $asset.sha256) {
        throw "Missing or changed bundled upstream asset: $($asset.path)"
    }
}

$inspectorCommand = Get-Command 'objdump' -ErrorAction SilentlyContinue
if (-not $inspectorCommand) {
    $inspectorCommand = Get-Command 'x86_64-w64-mingw32-objdump' -ErrorAction SilentlyContinue
}
if (-not $inspectorCommand) {
    throw 'objdump must be available on PATH to verify runtime DLL dependencies. No tool directories were searched or modified.'
}
$objectInspector = $inspectorCommand.Source

New-Item -ItemType Directory -Path $destination -Force | Out-Null
$packagedFiles = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

function Copy-PackageFile {
    param([string]$Source, [string]$RelativePath)
    $target = Join-Path $destination $RelativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $target -Force
    [void]$packagedFiles.Add($RelativePath.Replace('\', '/'))
}

function Copy-PackageTree {
    param([string]$Source, [string]$RelativePath)
    $sourcePrefix = [System.IO.Path]::GetFullPath($Source).TrimEnd([char[]]@('\', '/')) + [System.IO.Path]::DirectorySeparatorChar
    foreach ($file in (Get-ChildItem -LiteralPath $Source -File -Recurse -Force)) {
        $relativeFile = $file.FullName.Substring($sourcePrefix.Length)
        if ($relativeFile -match '(^|[\\/])(\.cache|\.langmake|__pycache__)([\\/]|$)' -or $relativeFile -match '\.pyc$') {
            continue
        }
        Copy-PackageFile -Source $file.FullName -RelativePath (Join-Path $RelativePath $relativeFile)
    }
}

function Write-PackageText {
    param([string]$RelativePath, [string]$Content)
    $target = Join-Path $destination $RelativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    [System.IO.File]::WriteAllText($target, $Content, [System.Text.UTF8Encoding]::new($false))
    [void]$packagedFiles.Add($RelativePath.Replace('\', '/'))
}

Copy-PackageFile -Source $executable -RelativePath 'langmake.exe'
foreach ($shader in @('vs_cubes.bin', 'fs_cubes.bin')) {
    Copy-PackageFile -Source (Join-Path $buildDirectory "shaders/$shader") -RelativePath "shaders/$shader"
}
Copy-PackageFile -Source (Join-Path $buildDirectory 'fonts/LatoLatin-Regular.ttf') -RelativePath 'fonts/LatoLatin-Regular.ttf'

# Inspect the executable's imports and then the imports of each copied DLL.
# Resolve redistributable DLLs only from the project-local build directory.
$runtimeLibraries = @{}
foreach ($library in (Get-ChildItem -LiteralPath $buildDirectory -Filter '*.dll' -File -Recurse)) {
    if (-not $runtimeLibraries.ContainsKey($library.Name)) {
        $runtimeLibraries[$library.Name] = $library.FullName
    }
}
# These are Windows-provided libraries, not files to redistribute from a host.
$windowsLibraries = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($name in @(
    'advapi32.dll', 'avrt.dll', 'bcrypt.dll', 'cfgmgr32.dll', 'combase.dll',
    'comctl32.dll', 'comdlg32.dll', 'crypt32.dll', 'd3d9.dll', 'd3d11.dll',
    'd3d12.dll', 'd3dcompiler_47.dll', 'dcomp.dll', 'dbghelp.dll', 'dinput8.dll',
    'dsound.dll', 'dwmapi.dll', 'dxgi.dll', 'gdi32.dll', 'hid.dll', 'imm32.dll',
    'iphlpapi.dll', 'kernel32.dll', 'ksuser.dll', 'mf.dll', 'mfplat.dll',
    'mfuuid.dll', 'msacm32.dll', 'msimg32.dll', 'msvcrt.dll', 'ncrypt.dll',
    'normaliz.dll', 'ntdll.dll', 'ole32.dll', 'oleaut32.dll', 'opengl32.dll',
    'powrprof.dll', 'propsys.dll', 'psapi.dll', 'rpcrt4.dll', 'secur32.dll',
    'setupapi.dll', 'shell32.dll', 'shcore.dll', 'shlwapi.dll', 'ucrtbase.dll',
    'user32.dll', 'userenv.dll', 'usp10.dll', 'uxtheme.dll', 'version.dll',
    'winhttp.dll', 'wininet.dll', 'winmm.dll', 'winspool.drv', 'wintrust.dll',
    'ws2_32.dll', 'wsock32.dll', 'wtsapi32.dll', 'xinput1_4.dll', 'xinput9_1_0.dll'
)) {
    [void]$windowsLibraries.Add($name)
}

$pending = [System.Collections.Generic.Queue[string]]::new()
$inspected = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$pending.Enqueue($executable)
while ($pending.Count -gt 0) {
    $binary = $pending.Dequeue()
    if (-not $inspected.Add($binary)) { continue }
    $importTable = & $objectInspector -p $binary 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Cannot inspect runtime imports for $binary. objdump exited with $LASTEXITCODE."
    }
    foreach ($line in $importTable) {
        if ([string]$line -notmatch '^\s*DLL Name:\s*(\S+)\s*$') { continue }
        $dependency = $Matches[1]
        if ($windowsLibraries.Contains($dependency) -or $dependency -match '^(api-ms-|ext-ms-)') {
            continue
        }
        if (-not $runtimeLibraries.ContainsKey($dependency)) {
            throw "Unresolved imported DLL '$dependency' from '$binary'. Put its redistributable file in build/ and include its license before packaging."
        }
        $libraryPath = $runtimeLibraries[$dependency]
        Copy-PackageFile -Source $libraryPath -RelativePath $dependency
        $pending.Enqueue($libraryPath)
    }
}

foreach ($example in $templates) {
    Copy-PackageTree -Source (Join-Path $projectRoot "examples/$example") -RelativePath "examples/$example"
    $exampleReadme = Join-Path $projectRoot "examples/$example/README.md"
    if (Test-Path -LiteralPath $exampleReadme -PathType Leaf) {
        $exampleText = [System.IO.File]::ReadAllText($exampleReadme).Replace('build/langmake.exe', './langmake.exe').Replace('from the repository', 'from the package folder').Replace('repository command', 'package command')
        Write-PackageText -RelativePath "examples/$example/README.md" -Content $exampleText
    }
}
Copy-PackageTree -Source (Join-Path $projectRoot 'licenses') -RelativePath 'licenses'
Copy-PackageTree -Source (Join-Path $projectRoot 'prompts') -RelativePath 'prompts'
Copy-PackageFile -Source (Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md') -RelativePath 'THIRD_PARTY_NOTICES.md'
Copy-PackageFile -Source (Join-Path $projectRoot 'dependencies.lock.json') -RelativePath 'dependencies.lock.json'
foreach ($androidFile in @('android.py', 'android.ps1', 'test_android.py', 'README.md', 'toolchain.lock.json', 'android-sdk-license.txt')) {
    Copy-PackageFile -Source (Join-Path $projectRoot "scripts/android/$androidFile") -RelativePath "scripts/android/$androidFile"
}
Copy-PackageTree -Source (Join-Path $projectRoot 'platform/android') -RelativePath 'platform/android'
foreach ($iosFile in @('ios.py', 'README.md', 'test_ios.py')) {
    Copy-PackageFile -Source (Join-Path $projectRoot "scripts/ios/$iosFile") -RelativePath "scripts/ios/$iosFile"
}
Copy-PackageFile -Source (Join-Path $projectRoot 'scripts/mobile_assets.py') -RelativePath 'scripts/mobile_assets.py'
Copy-PackageFile -Source (Join-Path $projectRoot 'scripts/test_mobile_assets.py') -RelativePath 'scripts/test_mobile_assets.py'
Copy-PackageTree -Source (Join-Path $projectRoot 'scripts/workflow') -RelativePath 'scripts/workflow'
Copy-PackageTree -Source (Join-Path $projectRoot 'scripts/windows') -RelativePath 'scripts/windows'
Copy-PackageTree -Source (Join-Path $projectRoot 'scripts/ui-editor') -RelativePath 'scripts/ui-editor'
Copy-PackageTree -Source (Join-Path $projectRoot 'scripts/music') -RelativePath 'scripts/music'
Copy-PackageTree -Source (Join-Path $projectRoot 'examples/music') -RelativePath 'examples/music'
if ($IncludeMusicAuthoring) {
    $musicBuild = Join-Path $projectRoot '.deps/music/build-report.json'
    if (-not (Test-Path -LiteralPath $musicBuild)) { throw 'Run langmake music setup before packaging optional music authoring.' }
    $musicInventory = Get-Content -LiteralPath $musicBuild -Raw | ConvertFrom-Json
    foreach ($entry in $musicInventory.files) {
        if ($entry.path -ne [System.IO.Path]::GetFileName($entry.path)) { throw 'Music binary inventory must contain filenames.' }
        $musicFile = Join-Path $projectRoot ".deps/music/bin/$($entry.path)"
        if ((Get-FileHash -LiteralPath $musicFile -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) { throw "Music binary inventory mismatch: $($entry.path)" }
        Copy-PackageFile -Source $musicFile -RelativePath "music-tools/bin/$($entry.path)"
    }
    Copy-PackageFile -Source $musicBuild -RelativePath 'music-tools/build-report.json'
}
if ($MusicExampleDirectory) {
    $musicExample = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $MusicExampleDirectory))
    if (-not $musicExample.StartsWith($workspacePrefix, [System.StringComparison]::OrdinalIgnoreCase)) { throw 'MusicExampleDirectory must be inside this workspace.' }
    & $executable validate --project $musicExample | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Music reference project is invalid.' }
    Copy-PackageTree -Source $musicExample -RelativePath 'examples/music-room'
}
foreach ($asset in $dependencyLock.ios_bundled_upstream_assets) {
    $assetPath = Join-Path $projectRoot $asset.source_path
    if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf) -or (Get-FileHash -LiteralPath $assetPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $asset.sha256) {
        throw "Missing or changed iOS upstream asset: $($asset.source_path)"
    }
    Copy-PackageFile -Source $assetPath -RelativePath "ios-player/$($asset.path)"
}
foreach ($notice in @('LICENSE', 'THIRD_PARTY_NOTICES.md', 'dependencies.lock.json')) {
    Copy-PackageFile -Source (Join-Path $projectRoot $notice) -RelativePath "ios-player/runtime/licenses/$notice"
}
Copy-PackageTree -Source (Join-Path $projectRoot 'licenses') -RelativePath 'ios-player/runtime/licenses/third-party'
Write-PackageText -RelativePath 'ios-player/README.md' -Content '# iOS staging resources

This folder contains pinned Metal shader binaries, a font and license notices for content staging. It does not contain a compiled iPhone player. Native builds require the matching source workspace and Mac/Xcode. Read [the iOS agent guide](../IOS-AGENT-GUIDE.md) and [qualification record](../IOS-QUALIFICATION.md).
'
$androidPlayer = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $AndroidPlayerDirectory))
if (-not $androidPlayer.StartsWith($workspacePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'AndroidPlayerDirectory must be inside the workspace.'
}
$androidManifest = Get-Content -LiteralPath (Join-Path $androidPlayer 'manifest.json') -Raw | ConvertFrom-Json
if ($androidManifest.engine_version -ne '0.5.0') { throw 'The Android player inventory must describe release 0.5.0.' }
$androidPrefix = $androidPlayer.TrimEnd([char[]]@('\', '/')) + [System.IO.Path]::DirectorySeparatorChar
$androidListed = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($entry in $androidManifest.files) {
    if (-not $androidListed.Add($entry.path)) { throw "Duplicate Android inventory entry: $($entry.path)" }
    $inputPath = [System.IO.Path]::GetFullPath((Join-Path $androidPlayer $entry.path))
    if (-not $inputPath.StartsWith($androidPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
        (Get-FileHash -LiteralPath $inputPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256 -or
        (Get-Item -LiteralPath $inputPath).Length -ne $entry.bytes) {
        throw "Invalid Android player inventory entry: $($entry.path)"
    }
    Copy-PackageFile -Source $inputPath -RelativePath "android-player/$($entry.path)"
}
foreach ($abi in @('arm64-v8a', 'x86_64')) {
    foreach ($configuration in @('debug', 'release')) {
        foreach ($name in @('libmain.so', 'player.json')) {
            if (-not $androidListed.Contains("$abi/$configuration/$name")) { throw "Android manifest omits $abi/$configuration/$name" }
        }
        $metadata = Get-Content -LiteralPath (Join-Path $androidPlayer "$abi/$configuration/player.json") -Raw | ConvertFrom-Json
        $libraryHash = (Get-FileHash -LiteralPath (Join-Path $androidPlayer "$abi/$configuration/libmain.so") -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($metadata.abi -ne $abi -or $metadata.configuration -ne $configuration -or $metadata.engine_version -ne '0.5.0' -or
            $metadata.sha256 -ne $libraryHash -or $metadata.development -isnot [bool] -or $metadata.development -ne ($configuration -eq 'debug')) {
            throw "Android player variant is incomplete: $abi/$configuration"
        }
        if (($inputCapabilities | ConvertFrom-Json).audio.feature_revision -ge 1 -and $metadata.audio_feature_revision -ne 1) {
            throw "Android player $abi/$configuration predates recorded audio; rebuild the variants and pass -AndroidPlayerDirectory with their new inventory."
        }
    }
}
foreach ($resource in @('runtime/shaders/vs_cubes.bin', 'runtime/shaders/fs_cubes.bin', 'runtime/fonts/LatoLatin-Regular.ttf')) {
    if (-not $androidListed.Contains($resource)) { throw "Android manifest omits $resource" }
}
Copy-PackageFile -Source (Join-Path $androidPlayer 'manifest.json') -RelativePath 'android-player/manifest.json'
foreach ($asset in $dependencyLock.android_bundled_upstream_assets) {
    $assetPath = Join-Path $destination $asset.path
    if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf) -or (Get-FileHash -LiteralPath $assetPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $asset.sha256) {
        throw "Missing or changed Android upstream asset: $($asset.path)"
    }
}
Copy-PackageFile -Source (Join-Path $projectRoot 'docs/releases/0.5.0-evidence.json') -RelativePath 'api/release-evidence.json'
Copy-PackageFile -Source (Join-Path $projectRoot 'docs/releases/ios-source-preview-evidence.json') -RelativePath 'api/ios-source-preview-evidence.json'
Copy-PackageTree -Source (Join-Path $projectRoot '.deps/source/SDL/android-project/app/src/main/java/org/libsdl/app') -RelativePath 'platform/android/app/src/main/java/org/libsdl/app'
$guideMap = [ordered]@{
    '08-agent-api.md'='AGENT-API.md'; '10-text-assets.md'='TEXT-ASSETS.md';
    '13-runtime-guide.md'='RUNTIME-GUIDE.md'; '14-release-evidence.md'='PREVIOUS-RELEASE-EVIDENCE.md';
    '15-game-creation-brief.md'='GAME-CREATION-BRIEF.md'; '16-studio-android-plan.md'='IMPLEMENTATION-STATUS.md';
    '17-studio.md'='STUDIO.md'; '18-android.md'='ANDROID.md'; '19-release-evidence.md'='PREVIOUS-STUDIO-ANDROID-EVIDENCE.md';
    '20-android-qualification.md'='ANDROID-QUALIFICATION.md'; '21-android-agent-guide.md'='ANDROID-AGENT-GUIDE.md';
    '22-ios-plan.md'='IOS.md'; '23-ios-agent-guide.md'='IOS-AGENT-GUIDE.md'; '24-ios-qualification.md'='IOS-QUALIFICATION.md';
    '25-workflow-implementation.md'='WORKFLOW-PROTOCOL.md'; '26-game-workflow.md'='GAME-WORKFLOW.md';
    '27-workflow-qualification.md'='RELEASE-EVIDENCE.md';
    '29-expressive-audio-plan.md'='MUSIC-PLAN.md'; '30-music-and-sound-guide.md'='MUSIC-AND-SOUND.md';
    '31-audio-qualification.md'='AUDIO-QUALIFICATION.md'; '39-script-execution-guide.md'='SCRIPT-EXECUTION.md';
    '33-ui-editor-plan.md'='UI-EDITOR-PLAN.md'; '34-ui-editor-guide.md'='UI-EDITOR.md'; '35-ui-editor-qualification.md'='UI-EDITOR-QUALIFICATION.md'; '37-studio-usability.md'='STUDIO-USABILITY.md'
}
function Convert-GuideLinks {
    param([string]$Content)
    foreach ($entry in $guideMap.GetEnumerator()) {
        $Content = $Content.Replace("($($entry.Key)", "($($entry.Value)")
        $Content = $Content.Replace("(../docs/$($entry.Key)", "($($entry.Value)")
    }
    return $Content.Replace('(../prompts/', '(prompts/').Replace('(../scripts/', '(scripts/').Replace('(../examples/', '(examples/').Replace('(releases/0.5.0-evidence.json', '(api/release-evidence.json').Replace('(releases/audio-feature-1-evidence.json', '(docs/releases/audio-feature-1-evidence.json').Replace('(releases/ui-editor-1-evidence.json', '(docs/releases/ui-editor-1-evidence.json').Replace('(../studio/README.md', '(STUDIO-DETAILS.md')
}
$agentGuide = [System.IO.File]::ReadAllText((Join-Path $projectRoot 'docs/08-agent-api.md'))
$agentGuide = $agentGuide.Replace('.\build\langmake.exe', '.\langmake.exe').Replace('./build/langmake.exe', './langmake.exe').Replace('repository root', 'package folder').Replace(' after [building the alpha](07-first-release.md)', '')
$agentGuide = $agentGuide.Replace('Build with `scripts/build.ps1`.', 'The executable in this package is ready to run; no build tools are required.')
$agentGuide = $agentGuide.Replace('(10-text-assets.md', '(TEXT-ASSETS.md')
$agentGuide = Convert-GuideLinks $agentGuide
$agentGuide = $agentGuide.Replace('(../prompts/', '(prompts/')
Write-PackageText -RelativePath 'AGENT-API.md' -Content $agentGuide
$assetGuide = [System.IO.File]::ReadAllText((Join-Path $projectRoot 'docs/10-text-assets.md'))
$assetGuide = $assetGuide.Replace('.\build\langmake.exe', '.\langmake.exe').Replace('./build/langmake.exe', './langmake.exe').Replace('(08-agent-api.md', '(AGENT-API.md')
$assetGuide = Convert-GuideLinks $assetGuide
Write-PackageText -RelativePath 'TEXT-ASSETS.md' -Content $assetGuide
foreach ($guide in $guideMap.GetEnumerator()) {
    if ($guide.Key -in @('08-agent-api.md', '10-text-assets.md')) { continue }
    $content = [System.IO.File]::ReadAllText((Join-Path $projectRoot "docs/$($guide.Key)"))
    $content = $content.Replace('.\build\langmake.exe', '.\langmake.exe').Replace('./build/langmake.exe', './langmake.exe')
    if ($guide.Key -eq '18-android.md') {
        $content = $content.Replace(' --host-build .cache/build-0.4', '').Replace('The source-build command is:', 'The portable export command is:').Replace('the source-build command is:', 'the portable export command is:').Replace('source-build command for this checkout', 'portable export command')
    }
    if ($guide.Key -eq '20-android-qualification.md') {
        $content = $content.Replace('From this checkout:', 'From this extracted runtime package:').Replace('& ".\dist\Langmake-Studio-0.5.0\Langmake Studio.exe"', '# Optional Studio: open Langmake Studio.exe from its separate distribution.').Replace('`docs/releases/0.5.0-evidence.json`', '`api/release-evidence.json`')
    }
    Write-PackageText -RelativePath $guide.Value -Content (Convert-GuideLinks $content)
}
$studioGuide = [System.IO.File]::ReadAllText((Join-Path $projectRoot 'studio/README.md'))
Write-PackageText -RelativePath 'STUDIO-DETAILS.md' -Content (Convert-GuideLinks $studioGuide)
Write-PackageText -RelativePath 'docs/22-ios-plan.md' -Content '# iOS implementation plan

Read [IOS.md](../IOS.md) for the implementation and Apple qualification gates.
'
Write-PackageText -RelativePath 'docs/23-ios-agent-guide.md' -Content '# iOS agent workflow

Read [IOS-AGENT-GUIDE.md](../IOS-AGENT-GUIDE.md) and [IOS-QUALIFICATION.md](../IOS-QUALIFICATION.md) before iPhone work. Native builds require the matching source checkout on an authorized Mac with Xcode; this Windows package alone is not an iOS build toolchain.
'
Write-PackageText -RelativePath 'docs/24-ios-qualification.md' -Content '# iOS qualification

Read [IOS-QUALIFICATION.md](../IOS-QUALIFICATION.md) for executed checks and pending Apple prerequisites.
'
Write-PackageText -RelativePath 'docs/21-android-agent-guide.md' -Content '# Android agent workflow

Read [ANDROID-AGENT-GUIDE.md](../ANDROID-AGENT-GUIDE.md) before creating or porting an Android game. It includes the ordered documentation list and complete author/test/build/install/handoff workflow.
'
Write-PackageText -RelativePath 'docs/18-android.md' -Content '# Android guide

Read [ANDROID.md](../ANDROID.md) in this package. This alias preserves the documentation path returned by capabilities.
'
Write-PackageText -RelativePath 'docs/20-android-qualification.md' -Content '# Android qualification

Read [ANDROID-QUALIFICATION.md](../ANDROID-QUALIFICATION.md) for executed checks, devices and remaining limits.
'
Write-PackageText -RelativePath 'docs/13-runtime-guide.md' -Content '# Runtime guide

Read [RUNTIME-GUIDE.md](../RUNTIME-GUIDE.md) in this package. This alias preserves the source-checkout path referenced by the executable schema. Subsystem objects are validated by the engine; the guide documents their authoring contracts.
'
foreach ($guide in $guideMap.GetEnumerator()) {
    $alias = "# Langmake documentation`n`nRead [$($guide.Value)](../$($guide.Value)) in this package. This preserves the canonical discovery path.`n"
    Write-PackageText -RelativePath "docs/$($guide.Key)" -Content $alias
}
# Keep canonical discovery and historical overview links directly readable.
# These are canonical sources; root-level guides adapt commands for the package.
foreach ($canonicalGuide in (Get-ChildItem -LiteralPath (Join-Path $projectRoot 'docs') -Filter '*.md' -File)) {
    $canonicalContent = [System.IO.File]::ReadAllText($canonicalGuide.FullName)
    $canonicalContent = $canonicalContent.Replace('.\build\langmake.exe', '.\langmake.exe').Replace('./build/langmake.exe', './langmake.exe').Replace('build/langmake.exe', 'langmake.exe')
    Write-PackageText -RelativePath "docs/$($canonicalGuide.Name)" -Content $canonicalContent
}
$readme = [System.IO.File]::ReadAllText((Join-Path $projectRoot 'README.md'))
$readme = $readme.Replace('(studio/README.md', '(STUDIO-DETAILS.md').Replace('build/langmake.exe', 'langmake.exe')
Write-PackageText -RelativePath 'README.md' -Content ("# Portable package entry point`n`nRead START-HERE.txt for launch instructions. The following source overview is included for feature/discovery context; compilation instructions apply to the separate source checkout.`n`n" + $readme)
foreach ($record in (Get-ChildItem -LiteralPath (Join-Path $projectRoot 'docs/releases') -Filter '*.json' -File)) {
    Copy-PackageFile -Source $record.FullName -RelativePath "docs/releases/$($record.Name)"
}
$agentInstructions = @'
# Working with the Langmake player package

Before game work, read START-HERE.txt and AGENT-API.md, beginning with its human-directed development and playtesting workflow. Read TEXT-ASSETS.md for native asset recipes and RUNTIME-GUIDE.md for multi-scene games, UI, animation, navigation, saves and audio. Use langmake.exe capabilities, schema and script-api for the implemented subset. This runtime package runs without a compiler or model connection. External Codex/Claude and other agents remain first-class clients. Optional Studio handles projects, chats and provider connections in a separate distribution; see STUDIO.md. Android tools are also independent of Studio; read ANDROID.md and ANDROID-QUALIFICATION.md for setup, executed device checks and limits.

Start each game with langmake.exe context --project GAME and read GAME-WORKFLOW.md. Per-game targets live in ignored .langmake/targets.local.json and must be explicitly configured for each game/machine. Never inherit another game's SSH/phone/team settings. Optional Python workflow commands provide target setup, resumable deployment, verification, receipts and handoff. Use the workflow-2d starter for shared presentation profiles, held action controls, application icons and save examples. Player exports run without Python or Studio.

For every Android game or port request, start with ANDROID-AGENT-GUIDE.md and its ordered documentation list before authoring. Follow its explicit paths, target setup, touch/save authoring, host checks, APK export, native device request/receipt rules and human handoff. Android commands differ from desktop commands. Engine changes must keep this guide, Android tool reference, discovery links and affected prompts aligned.

For iOS work, read IOS-AGENT-GUIDE.md and IOS-QUALIFICATION.md. iOS tooling remains independent of Studio. Staging on Windows does not compile an iPhone app: use the matching engine source on an authorized Mac with Xcode, explicit signing and destination settings. Do not claim qualification from generated files or historical Windows/Android evidence.

For optional prompt structures and filled game briefs, read prompts/README.md. Humans and agents can use these examples for new games, feedback, defects, assets and planning. They are inspiration, not authorization or evidence of built games. The human's actual direction takes precedence. Engine maintainers review these canonical prompts with relevant engine changes and regenerate packaged copies; see their maintenance checklist and the source checkout's AGENTS.md.

For human UI corrections, read UI-EDITOR.md and the game's ui-design.json when present. Preserve stable IDs, bindings and pinned intent. The optional CLI at scripts/ui-editor/ui-editor.cjs shares Studio's isolated native drafts and review export contract.

For music and sound, read MUSIC-AND-SOUND.md, AUDIO-QUALIFICATION.md and prompts/12-music-and-sound.md. The native player supports recorded WAV/FLAC stems and adaptive layers; optional music authoring uses Python and sfizz on the host. Run langmake.exe music status to check availability. The source bank is a separate pinned download. Technical audio checks do not establish musical approval; the human rejected the initial showcase arrangements as artificial.

The human is the decision-making developer and primary gameplay playtester. AI agents are worker developers who implement the human's ideas, run technical checks and prepare playable iterations. Complete authorized work autonomously without requesting permission for every edit. At meaningful playable milestones, provide the correct launch command, controls, changes, known issues and focused feedback prompts. Human feedback directs the next creative iteration.

Prefer native validate, simulate, inspect and capture commands for technical verification. Do not default to computer-use gameplay or autonomous full playthroughs in place of human playtesting. Supplementary agent playtests are appropriate when requested or for a focused defect; use computer use only when requested or a specific input/UI behavior cannot be checked with native commands. Passing checks do not establish human gameplay acceptance. If feedback is needed, report that the milestone is ready and awaiting human playtesting; do not invent approval.

Do not disrupt an active human play session with watched source edits, reloads, resets, pauses or injected input unless requested. Continue independent authorized work where possible. Discussing an idea is not authorization to implement it. Stay within the workspace and scope the human authorized.

Create games with langmake.exe new in a fresh project directory and preserve the bundled examples as references. This is a Windows/D3D11 and Android/GLES3 alpha; consult ANDROID-QUALIFICATION.md for tested devices and limits. Native sources and build tools are not included in this player package.
'@
Write-PackageText -RelativePath 'AGENTS.md' -Content $agentInstructions
if (Test-Path -LiteralPath (Join-Path $projectRoot 'LICENSE') -PathType Leaf) {
    Copy-PackageFile -Source (Join-Path $projectRoot 'LICENSE') -RelativePath 'LICENSE'
}

foreach ($dimension in $templates) {
    $launcher = @'
@echo off
setlocal
cd /d "%~dp0"
"langmake.exe" play --project "examples\DIMENSION"
set "langmake_exit=%errorlevel%"
if not "%langmake_exit%"=="0" pause
exit /b %langmake_exit%
'@
    $launcherText = $launcher.Replace('DIMENSION', $dimension).Replace("`r`n", "`n").Replace("`n", "`r`n")
    Write-PackageText -RelativePath "run-$dimension.cmd" -Content $launcherText
}

$instructions = @'
Langmake 0.5.0 - Windows and Android alpha

Keep all files in this folder together. Double-click run-rockets-2d.cmd or
run-rockets-3d.cmd for the text-authored Star Patrol demos with synthesized music.
The original platform games remain available through run-2d.cmd and run-3d.cmd.
Try run-adventure.cmd for dialogue, quests, equipment, saves and area transitions;
run-strategy.cmd for group navigation; run-shared-2d.cmd for shared game services.
Try run-workflow-2d.cmd for a small shared-platform game with held LIGHT controls,
pause/save/load, application icon, presentation profiles and named scenarios.
The adventure, strategy and shared-2d projects have README files describing
their exact controls and scope. The original platform/Star Patrol controls follow.

Controls: A/D or arrows move; W/S move in 3D; Space fires in Star Patrol
and jumps in the platform games. Close the window to quit play mode.
Launchers use play: no developer overlay, source watching or injected controls.
Use preview explicitly for P pause, R reset, F5 reload, camera inspection,
F12 captures, Escape to quit and native live controls. UI focus consumes relevant game input.

No compiler, model or LLM account is needed to run this package.
For a new game, use langmake.exe new and keep the examples as references.
Edit the cloned game's Luau and JSON files to change it.
The human is the decision-making developer and primary gameplay playtester.
AI agents implement the human's ideas and run technical checks, then hand over
playable iterations with controls and focused questions for human feedback.
Computer-use playthroughs are supplementary, not the default replacement for
the human playing. Agents must read AGENTS.md and the human-directed workflow
at the start of AGENT-API.md before creating a game.
AGENT-API.md documents the project format, Luau functions, patches and live controls.
TEXT-ASSETS.md documents sprites, procedural meshes, music scores and sound effects.
MUSIC-AND-SOUND.md covers recorded instruments, text compositions, imported audio,
adaptive stems and effects. AUDIO-QUALIFICATION.md separates technical results
from human listening feedback. Run langmake.exe music status for setup.
RUNTIME-GUIDE.md documents the additional runtime systems and their limits.
RELEASE-EVIDENCE.md separates technical verification from human gameplay acceptance.
GAME-CREATION-BRIEF.md is a starting brief for your next external AI agent.
GAME-WORKFLOW.md explains per-game target setup, reusable verification, icons,
resumable signing and handoff. Python is required for authoring/export workflow
commands; exported games do not require it. No host/device setup is preconfigured.
prompts/README.md is the prompt library for humans and agents: fill-in
structures, 2D/adventure/strategy examples, feedback, bugs, assets and planning.

From a terminal in this folder:
  langmake.exe capabilities
  langmake.exe schema
  langmake.exe asset schema --type sprite
  langmake.exe asset schema --type mesh
  langmake.exe asset schema --type music
  langmake.exe asset schema --type model
  langmake.exe script-api
  langmake.exe new --template adventure --output my-adventure
  langmake.exe new --template workflow-2d --output my-game
  langmake.exe context --project my-game
  langmake.exe verify --project my-game --scenario delivery --profile desktop
  langmake.exe new --template rockets-2d --output my-star-game
  langmake.exe asset list --project examples/rockets-2d
  langmake.exe asset render --project examples/rockets-2d --id rocket --output rocket.png --scale 4
  langmake.exe asset render --project examples/rockets-2d --id theme --output theme.wav
  langmake.exe validate --project examples/2d
  langmake.exe check --project examples/adventure
  langmake.exe inspect --project examples/3d
  langmake.exe inspect --project examples/3d --live
  langmake.exe simulate --project examples/2d --steps 120
  langmake.exe new --dimension 2d --output my-game
  langmake.exe preview --project examples/3d
  langmake.exe play --project examples/adventure
  langmake.exe capture --project examples/3d --output capture.png --frames 2
  langmake.exe capture --project examples/3d --steps 60 --ticks 0,30,60 --output-dir captures

While a preview is open, a second terminal can send live commands:
  langmake.exe control --project examples/3d --action pause
  langmake.exe control --project examples/3d --action step --steps 60
  langmake.exe control --project examples/3d --action resume
Other live actions: reset, snapshot, capture, save, load. Step accepts --input input.json.
Control replies are compact; add --include-state when a full state is needed.
Inspection supports --ids and --fields; --check-source explicitly hashes sources.
Timeline captures run in isolation and do not take over an active play session.

This package is verified on Windows. It is a portable development package,
not a signed installer. Optional Studio ships separately with this runtime.
STUDIO.md describes projects/chats and external-agent handoff. ANDROID.md
describes Android game export and touch authoring. This package includes
ARM64 and x86_64 Android players in debug and release variants. Export uses
Python and a workspace-local Android SDK/JDK/Gradle setup; no C++ compiler
or engine sources are required. Read the SDK terms before accepting setup.
ANDROID-QUALIFICATION.md records physical-phone and emulator checks, controls
and limits. VR, consoles and other platforms remain future work.
ANDROID-AGENT-GUIDE.md is the required starting workflow and reading list for
agents creating Android games, whether using external tools or optional Studio.
IOS-AGENT-GUIDE.md and IOS-QUALIFICATION.md describe iOS source work and Apple
build prerequisites. This Windows package can stage content but has no Xcode
toolchain or precompiled iPhone app. Native iOS builds require matching engine
sources in an authorized Mac workspace. Read the current qualification record
for measured simulator/phone coverage and remaining device/release limits.
See THIRD_PARTY_NOTICES.md and licenses/ for third-party license terms.
package-manifest.json identifies files written by the packaging operation.
'@
Write-PackageText -RelativePath 'START-HERE.txt' -Content $instructions

$packageExecutable = Join-Path $destination 'langmake.exe'
Push-Location $destination
try {
    $capabilities = & $packageExecutable capabilities
    if ($LASTEXITCODE -ne 0) {
        throw "Packaged executable smoke check failed with exit code $LASTEXITCODE."
    }
    $capabilityData = ($capabilities -join "`n") | ConvertFrom-Json
    if (-not $capabilityData.ok -or $capabilityData.version -ne '0.5.0') {
        throw 'Packaging requires the current Langmake 0.5.0 executable.'
    }
    Write-PackageText -RelativePath 'api/capabilities.json' -Content ($capabilities -join "`n")
    $projectSchema = & $packageExecutable schema
    if ($LASTEXITCODE -ne 0) { throw 'Packaged project schema discovery failed.' }
    Write-PackageText -RelativePath 'api/project.schema.json' -Content ($projectSchema -join "`n")
    foreach ($assetType in @('sprite', 'mesh', 'music', 'model')) {
        $assetSchema = & $packageExecutable asset schema --type $assetType
        if ($LASTEXITCODE -ne 0) { throw "Packaged $assetType schema discovery failed." }
        Write-PackageText -RelativePath "api/$assetType.schema.json" -Content ($assetSchema -join "`n")
    }
    $scriptApi = & $packageExecutable script-api
    if ($LASTEXITCODE -ne 0) { throw 'Packaged script API discovery failed.' }
    Write-PackageText -RelativePath 'api/script-api.json' -Content ($scriptApi -join "`n")
    Write-PackageText -RelativePath 'api/engine.d.luau' -Content (($scriptApi -join "`n" | ConvertFrom-Json).definitions)
}
finally {
    Pop-Location
}

$manifestEntries = foreach ($relativePath in ($packagedFiles | Sort-Object)) {
    $file = Get-Item -LiteralPath (Join-Path $destination $relativePath)
    [ordered]@{
        path = $relativePath
        bytes = $file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$manifest = [ordered]@{
    format_version = 1
    product = 'Langmake'
    version = '0.5.0'
    audio_feature_revision = ($inputCapabilities | ConvertFrom-Json).audio.feature_revision
    music_authoring_binaries = [bool]$IncludeMusicAuthoring
    platform = 'windows-x64'
    created_utc = [DateTime]::UtcNow.ToString('o')
    files = @($manifestEntries)
}
$manifestText = $manifest | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText((Join-Path $destination 'package-manifest.json'), $manifestText, [System.Text.UTF8Encoding]::new($false))
Write-Host "Packaged $($packagedFiles.Count) files and package-manifest.json in $destination"
Write-Host 'The packaged executable passed its capabilities smoke check.'
Write-Host 'Run scripts/package_smoke.py to verify all eight starters and create the ZIP.'
