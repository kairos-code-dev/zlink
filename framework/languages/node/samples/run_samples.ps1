Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $scriptDir "TicTacToe.Ts/run_sample.ps1")
& (Join-Path $scriptDir "Bingo.Ts/run_sample.ps1")
