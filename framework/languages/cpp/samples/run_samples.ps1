$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

& (Join-Path $ScriptDir "TicTacToe/run_sample.ps1")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $ScriptDir "Bingo/run_sample.ps1")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $ScriptDir "DeliveryDispatch/run_sample.ps1")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $ScriptDir "GameQuest/run_sample.ps1")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $ScriptDir "ShoppingMall/run_sample.ps1")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $ScriptDir "ShoppingMallCheckout/run_sample.ps1")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
