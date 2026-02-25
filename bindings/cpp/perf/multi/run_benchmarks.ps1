$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Resolve-Path (Join-Path $ScriptDir "..\..\..\..")
$Runner = Join-Path $RootDir "bindings\perf\run_policy_bench.py"
if (Get-Command py -ErrorAction SilentlyContinue) {
    & py $Runner --binding cpp --suite multi @Args
} else {
    & python $Runner --binding cpp --suite multi @Args
}
exit $LASTEXITCODE
