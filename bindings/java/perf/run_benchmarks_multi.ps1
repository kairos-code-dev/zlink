$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Resolve-Path (Join-Path $ScriptDir "../../..")
if (-not $env:PERF_MULTI_CLIENTS) {
    $env:PERF_MULTI_CLIENTS = "100"
}
& (Join-Path $RootDir "bindings/perf/run_policy_bench.py") --binding java --suite multi @args
exit $LASTEXITCODE
