$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $ScriptDir "../run_benchmarks.ps1") @args
exit $LASTEXITCODE
