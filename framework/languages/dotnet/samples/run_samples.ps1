$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

& (Join-Path $ScriptDir "TicTacToe/run_sample.ps1")
& (Join-Path $ScriptDir "Bingo/run_sample.ps1")
& (Join-Path $ScriptDir "SupportChat/run_sample.ps1")
& (Join-Path $ScriptDir "ShoppingMallCheckout/run_sample.ps1")
& (Join-Path $ScriptDir "GameQuest/run_sample.ps1")
