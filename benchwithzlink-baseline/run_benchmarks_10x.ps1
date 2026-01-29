# Run benchmarks 10 times for statistical reliability with CPU affinity pinning
# CPU affinity reduces variance by preventing core migration and cache misses
# Output: CSV format for easy analysis

$ErrorActionPreference = "Stop"

$BENCH_DIR = "D:\project\ulalax\zlink\build\windows-x64\bin\Release"
$ITERATIONS = 10

Write-Host "===================================" -ForegroundColor Cyan
Write-Host "Benchmark Configuration" -ForegroundColor Cyan
Write-Host "===================================" -ForegroundColor Cyan
Write-Host "Iterations:    $ITERATIONS"
Write-Host "CPU Affinity:  Enabled (using start /affinity 1)"
Write-Host "Output:        results_10x.csv"
Write-Host "===================================" -ForegroundColor Cyan
Write-Host ""

# Benchmark configurations: pattern, zlink_prog, libzlink_prog, message_sizes
$benchmarks = @(
    @{pattern="PAIR"; zlink="comp_zlink_pair.exe"; libzlink="comp_std_zlink_pair.exe"; sizes=@(64, 1024)},
    @{pattern="PUBSUB"; zlink="comp_zlink_pubsub.exe"; libzlink="comp_std_zlink_pubsub.exe"; sizes=@(64, 1024)},
    @{pattern="DEALER_DEALER"; zlink="comp_zlink_dealer_dealer.exe"; libzlink="comp_std_zlink_dealer_dealer.exe"; sizes=@(64)},
    @{pattern="DEALER_ROUTER"; zlink="comp_zlink_dealer_router.exe"; libzlink="comp_std_zlink_dealer_router.exe"; sizes=@(64)},
    @{pattern="ROUTER_ROUTER"; zlink="comp_zlink_router_router.exe"; libzlink="comp_std_zlink_router_router.exe"; sizes=@(64)},
    @{pattern="ROUTER_ROUTER_POLL"; zlink="comp_zlink_router_router_poll.exe"; libzlink="comp_std_zlink_router_router_poll.exe"; sizes=@(64)}
)

# CSV Header
"Iteration,Library,Pattern,Transport,MsgSize,Metric,Value" | Out-File -FilePath "results_10x.csv" -Encoding UTF8

foreach ($bench in $benchmarks) {
    $pattern = $bench.pattern
    $zlink_prog = $bench.zlink
    $libzlink_prog = $bench.libzlink

    foreach ($size in $bench.sizes) {
        Write-Host "Running: $pattern (${size}-byte) - 10 iterations with CPU affinity" -ForegroundColor Yellow

        for ($i = 1; $i -le $ITERATIONS; $i++) {
            Write-Host "  Iteration $i/$ITERATIONS..." -NoNewline

            # Run zlink benchmark
            $cmd_zlink = "$BENCH_DIR\$zlink_prog zlink tcp $size"
            $output_zlink = cmd /c "start /affinity 1 /wait /b $cmd_zlink" 2>&1 | Out-String

            # Run libzlink benchmark
            $cmd_libzlink = "$BENCH_DIR\$libzlink_prog libzlink tcp $size"
            $output_libzlink = cmd /c "start /affinity 1 /wait /b $cmd_libzlink" 2>&1 | Out-String

            # Parse zlink output (format: RESULT,lib,pattern,transport,size,metric,value)
            $results_zlink = $output_zlink -split "`n" | Where-Object { $_ -match "^RESULT," }
            foreach ($result in $results_zlink) {
                $parts = $result -split ","
                if ($parts.Length -ge 7) {
                    $metric = $parts[5].Trim()
                    $value = $parts[6].Trim()
                    "$i,zlink,$pattern,tcp,$size,$metric,$value" | Out-File -FilePath "results_10x.csv" -Append -Encoding UTF8
                }
            }

            # Parse libzlink output
            $results_libzlink = $output_libzlink -split "`n" | Where-Object { $_ -match "^RESULT," }
            foreach ($result in $results_libzlink) {
                $parts = $result -split ","
                if ($parts.Length -ge 7) {
                    $metric = $parts[5].Trim()
                    $value = $parts[6].Trim()
                    "$i,libzlink,$pattern,tcp,$size,$metric,$value" | Out-File -FilePath "results_10x.csv" -Append -Encoding UTF8
                }
            }

            Write-Host " Done" -ForegroundColor Green
        }

        Write-Host ""
    }
}

Write-Host "===================================" -ForegroundColor Green
Write-Host "Benchmark runs completed!" -ForegroundColor Green
Write-Host "Results saved to: results_10x.csv" -ForegroundColor Green
Write-Host "===================================" -ForegroundColor Green
