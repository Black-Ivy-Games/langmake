param([string]$NodePath,[string]$NpmCli,[switch]$Install,[switch]$Test)
$ErrorActionPreference='Stop'
$studioRoot=$PSScriptRoot
$cache=Join-Path $studioRoot '.cache'
New-Item -ItemType Directory -Force -Path $cache,(Join-Path $cache 'tmp') | Out-Null
$env:npm_config_cache=Join-Path $cache 'npm'
$env:ELECTRON_CACHE=Join-Path $cache 'electron'
$env:TEMP=Join-Path $cache 'tmp'; $env:TMP=$env:TEMP
$electron=Join-Path $studioRoot 'node_modules/electron/dist/electron.exe'
if($Install -or !(Test-Path -LiteralPath $electron) -or $Test){
    if(!$NodePath){$NodePath=(Get-Command node.exe -ErrorAction Stop).Source}
    $version=[version]((& $NodePath --version).TrimStart('v'))
    if($version -lt [version]'22.12.0'){throw "Studio setup needs Node22.12 or later. Supply -NodePath to a suitable executable; no global tools are installed."}
    if(!$NpmCli){$npmCommand=(Get-Command npm.cmd -ErrorAction Stop).Source;$NpmCli=Join-Path (Split-Path $npmCommand) 'node_modules/npm/bin/npm-cli.js'}
    Push-Location $studioRoot
    try{
        if($Install -or !(Test-Path -LiteralPath $electron)){& $NodePath $NpmCli ci --no-audit --no-fund;if($LASTEXITCODE){throw 'npm ci failed'};& $NodePath 'node_modules/electron/install.js';if($LASTEXITCODE){throw 'Electron runtime download failed'}}
        if($Test){& $NodePath --test 'test/core.test.cjs' 'test/ios.test.cjs';if($LASTEXITCODE){throw 'Studio core tests failed'};return}
    }finally{Pop-Location}
}
# Electron owns the visible application window; suppress a separate helper console.
Start-Process -FilePath $electron -ArgumentList @('"'+$studioRoot+'"') -WorkingDirectory $studioRoot -WindowStyle Hidden
