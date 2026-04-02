$ErrorActionPreference = "Stop"
$root = "/home/hep7/project/kairos/zlink/bindings/dotnet/samples"
$coreLib = "/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so"

if (Test-Path $coreLib) {
    $env:ZLINK_LIBRARY_PATH = $coreLib
}

$samples = @(
    "PairRecv/PairRecv.csproj"
    "PairCallback/PairCallback.csproj"
    "MonitorRecv/MonitorRecv.csproj"
    "PubSubRecv/PubSubRecv.csproj"
    "PubSubCallback/PubSubCallback.csproj"
    "DealerRouterRecv/DealerRouterRecv.csproj"
    "DealerRouterCallback/DealerRouterCallback.csproj"
    "StreamRecv/StreamRecv.csproj"
    "StreamCallback/StreamCallback.csproj"
    "SpotRecv/SpotRecv.csproj"
    "SpotCallback/SpotCallback.csproj"
)

$passed = 0
$failed = 0

foreach ($sample in $samples) {
    Write-Host "RUN,$sample"
    & dotnet run --project "$root/$sample"
    if ($LASTEXITCODE -eq 0) {
        Write-Host "OK,$sample"
        $passed++
    }
    else {
        Write-Host "FAIL,$sample"
        $failed++
    }
}

Write-Host "SUMMARY,passed,$passed,failed,$failed,total,$($samples.Count)"
if ($failed -ne 0) {
    exit 1
}
