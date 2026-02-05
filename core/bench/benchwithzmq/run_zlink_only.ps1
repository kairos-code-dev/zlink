# Run zlink benchmarks only (no libzmq comparison)
# CPU affinity enabled for stable measurements

$ErrorActionPreference = "Stop"

$BENCH_DIR = "D:\project\ulalax\zlink\core\build\windows-x64\bin\Release"
$ITERATIONS = 10

Write-Host "===================================" -ForegroundColor Cyan
Write-Host "zlink Benchmark (standalone)" -ForegroundColor Cyan
Write-Host "===================================" -ForegroundColor Cyan
Write-Host "Iterations:    $ITERATIONS"
Write-Host "Output:        zlink_results.csv"
Write-Host "===================================" -ForegroundColor Cyan
Write-Host ""

# Benchmark configurations
$benchmarks = @(
    @{pattern="PAIR"; prog="comp_zlink_pair.exe"; sizes=@(64, 1024)},
    @{pattern="PUBSUB"; prog="comp_zlink_pubsub.exe"; sizes=@(64, 1024)},
    @{pattern="DEALER_DEALER"; prog="comp_zlink_dealer_dealer.exe"; sizes=@(64)},
    @{pattern="DEALER_ROUTER"; prog="comp_zlink_dealer_router.exe"; sizes=@(64)},
    @{pattern="ROUTER_ROUTER"; prog="comp_zlink_router_router.exe"; sizes=@(64)},
    @{pattern="ROUTER_ROUTER_POLL"; prog="comp_zlink_router_router_poll.exe"; sizes=@(64)},
)

# CSV Header
"Iteration,Pattern,Transport,MsgSize,Metric,Value" | Out-File -FilePath "zlink_results.csv" -Encoding UTF8

foreach ($bench in $benchmarks) {
    $pattern = $bench.pattern
    $prog = $bench.prog

    foreach ($size in $bench.sizes) {
        Write-Host "Running: $pattern (${size}-byte) - $ITERATIONS iterations" -ForegroundColor Yellow

        for ($i = 1; $i -le $ITERATIONS; $i++) {
            Write-Host "  Iteration $i/$ITERATIONS..." -NoNewline

            # Run benchmark directly
            $output = & "$BENCH_DIR\$prog" "zlink" "tcp" $size 2>&1 | Out-String

            # Parse output
            $results = $output -split "`n" | Where-Object { $_ -match "^RESULT," }
            foreach ($result in $results) {
                $parts = $result -split ","
                if ($parts.Length -ge 7) {
                    $metric = $parts[5].Trim()
                    $value = $parts[6].Trim()
                    "$i,$pattern,tcp,$size,$metric,$value" | Out-File -FilePath "zlink_results.csv" -Append -Encoding UTF8
                }
            }

            Write-Host " Done" -ForegroundColor Green
        }

        Write-Host ""
    }
}

Write-Host "===================================" -ForegroundColor Green
Write-Host "zlink benchmarks completed!" -ForegroundColor Green
Write-Host "Results saved to: zlink_results.csv" -ForegroundColor Green
Write-Host "===================================" -ForegroundColor Green
