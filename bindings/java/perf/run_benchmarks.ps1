$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Resolve-Path (Join-Path $ScriptDir "../../..")
& (Join-Path $RootDir "bindings/perf/run_policy_bench.py") --binding java --suite single @args
exit $LASTEXITCODE
