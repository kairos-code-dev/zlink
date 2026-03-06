$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $ScriptDir "../run_benchmarks_multi.ps1") @args
exit $LASTEXITCODE
