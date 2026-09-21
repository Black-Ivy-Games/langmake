# Pass arguments unchanged to the workspace-local Android tool. Examples:
# .\scripts\android\android.ps1 doctor
# .\scripts\android\android.ps1 build --project examples/2d --abi arm64-v8a
$ErrorActionPreference = 'Stop'
$tool = Join-Path $PSScriptRoot 'android.py'
& python $tool @args
exit $LASTEXITCODE
