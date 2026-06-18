Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$cppRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
$buildDir = if ($env:ZLINK_CPP_BUILD_DIR) { $env:ZLINK_CPP_BUILD_DIR } else { Join-Path $cppRoot "build" }
$server = Join-Path $buildDir "sample_cpp_framework_shoppingmall_server"
$client = Join-Path $buildDir "sample_cpp_framework_shoppingmall_client"
if (-not (Test-Path $client)) {
    $server = Join-Path $buildDir "linux-ninja-debug/sample_cpp_framework_shoppingmall_server"
    $client = Join-Path $buildDir "linux-ninja-debug/sample_cpp_framework_shoppingmall_client"
}

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse("127.0.0.1"), 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$listener.Stop()
$env:SHOPPINGMALL_WORKFLOW_ENDPOINT = "tcp://127.0.0.1:$port"

$serverProcess = Start-Process -FilePath $server -PassThru
try {
    for ($i = 0; $i -lt 100; $i++) {
        try {
            $clientProbe = [System.Net.Sockets.TcpClient]::new("127.0.0.1", $port)
            $clientProbe.Close()
            break
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    & $client
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    if (-not $serverProcess.HasExited) {
        Stop-Process -Id $serverProcess.Id -Force
    }
}
