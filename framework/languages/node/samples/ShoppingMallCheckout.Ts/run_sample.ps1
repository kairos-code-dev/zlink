Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $scriptDir
$serverProcess = $null
try {
    npm run build | Out-Null
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse("127.0.0.1"), 0)
    $listener.Start()
    $port = $listener.LocalEndpoint.Port
    $listener.Stop()
    $env:SHOPPINGMALLCHECKOUT_CHECKOUT_ENDPOINT = "tcp://127.0.0.1:$port"
    $env:ZLINK_SAMPLE_CONFIG = Join-Path ([System.IO.Path]::GetTempPath()) "shoppingmallcheckout-sample.env"

    function Wait-Port {
        param([int]$Port)
        $deadline = (Get-Date).AddSeconds(10)
        while ((Get-Date) -lt $deadline) {
            try {
                $client = [System.Net.Sockets.TcpClient]::new()
                $client.Connect("127.0.0.1", $Port)
                $client.Close()
                return
            } catch {
                Start-Sleep -Milliseconds 100
            }
        }
        throw "Timed out waiting for ShoppingMallCheckout server"
    }

    function Start-Server {
        Start-Process -FilePath "node" -ArgumentList @((Join-Path $scriptDir "dist/Server/main.js")) -NoNewWindow -PassThru
    }

    $serverProcess = Start-Server
    Wait-Port -Port $port
    node (Join-Path $scriptDir "dist/Client/main.js")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    if ($null -ne $serverProcess -and -not $serverProcess.HasExited) {
        Stop-Process -Id $serverProcess.Id -Force
    }
    Pop-Location
}
