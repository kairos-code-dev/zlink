$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
node (Join-Path $scriptDir "../run-sample.mjs") GameQuest.Ts
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
