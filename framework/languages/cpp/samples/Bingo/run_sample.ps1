$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$CppRoot = Resolve-Path (Join-Path $ScriptDir "../..")
$BuildDir = if ($env:ZLINK_CPP_BUILD_DIR) { $env:ZLINK_CPP_BUILD_DIR } else { Join-Path $CppRoot "build" }
$CTestBin = if ($env:CTEST_BIN) { $env:CTEST_BIN } else { "ctest" }
$LogDir = Join-Path $ScriptDir "build/sample-logs"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $LogDir "*.log")

function Find-Binary([string]$Name) {
    $candidates = @(
        (Join-Path $BuildDir $Name),
        (Join-Path $BuildDir "$Name.exe"),
        (Join-Path $BuildDir "linux-ninja-debug/$Name"),
        (Join-Path $BuildDir "linux-ninja-debug/$Name.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    throw "Missing executable: $Name. Build C++ samples first or set ZLINK_CPP_BUILD_DIR."
}

$RegistryBin = Find-Binary "sample_cpp_framework_bingo_registry"
$ApiBin = Find-Binary "sample_cpp_framework_bingo_api"
$PlayBin = Find-Binary "sample_cpp_framework_bingo_play"
$SessionBin = Find-Binary "sample_cpp_framework_bingo_session"
$ClientBin = Find-Binary "sample_cpp_framework_bingo_client"

$Processes = New-Object System.Collections.Generic.List[System.Diagnostics.Process]
$RedisContainer = $null
$RedisKeyPrefix = if ($env:BINGO_REDIS_KEY_PREFIX) { $env:BINGO_REDIS_KEY_PREFIX } else { "bingo:cpp:${PID}:$([Guid]::NewGuid().ToString('N')):" }

function Print-Logs([int]$Status) {
    if ($Status -eq 0) {
        return
    }
    Get-ChildItem -Path $LogDir -Filter "*.log" -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Host "===== $($_.FullName) ====="
        Get-Content -Path $_.FullName -Tail 200 -ErrorAction SilentlyContinue | ForEach-Object {
            Write-Host $_
        }
    }
}

function Cleanup([int]$Status) {
    Print-Logs $Status
    for ($i = $Processes.Count - 1; $i -ge 0; $i--) {
        $process = $Processes[$i]
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
    foreach ($process in $Processes) {
        try {
            $process.WaitForExit(1000) | Out-Null
        } catch {
        }
    }
    if ($RedisContainer) {
        & docker rm -f $RedisContainer 2>$null | Out-Null
    }
}

function Reserve-Endpoints([int]$Count) {
    if ($env:BINGO_BASE_PORT) {
        $basePort = [int]$env:BINGO_BASE_PORT
        $fixed = New-Object System.Collections.Generic.List[string]
        for ($i = 1; $i -le $Count; $i++) {
            $fixed.Add("127.0.0.1:$($basePort + $i)")
        }
        return $fixed.ToArray()
    }

    $listeners = New-Object System.Collections.Generic.List[System.Net.Sockets.TcpListener]
    $endpoints = New-Object System.Collections.Generic.List[string]
    $used = [System.Collections.Generic.HashSet[int]]::new()
    $blocked = [System.Collections.Generic.HashSet[int]]::new()
    try {
        while ($endpoints.Count -lt $Count) {
            $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse("127.0.0.1"), 0)
            $listener.Start()
            $port = [int]$listener.LocalEndpoint.Port
            $ctrlPort = $port + 1000
            if ($ctrlPort -gt 65535 -or $used.Contains($port) -or $blocked.Contains($port) -or
                $used.Contains($ctrlPort) -or $blocked.Contains($ctrlPort)) {
                $listener.Stop()
                continue
            }
            $ctrl = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse("127.0.0.1"), $ctrlPort)
            try {
                $ctrl.Start()
            } catch {
                $listener.Stop()
                continue
            } finally {
                if ($ctrl) {
                    $ctrl.Stop()
                }
            }
            [void]$used.Add($port)
            [void]$blocked.Add($ctrlPort)
            $listeners.Add($listener)
            $endpoints.Add("127.0.0.1:$port")
        }
        return $endpoints.ToArray()
    } finally {
        foreach ($listener in $listeners) {
            $listener.Stop()
        }
    }
}

function Split-Endpoint([string]$Endpoint) {
    $value = $Endpoint -replace '^tcp://', '' -replace '^http://', ''
    $index = $value.LastIndexOf(':')
    return @{ Host = $value.Substring(0, $index); Port = [int]$value.Substring($index + 1) }
}

function Wait-Endpoint([string]$Name, [string]$Endpoint, [int]$TimeoutSeconds = 60) {
    $parts = Split-Endpoint $Endpoint
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $client = [System.Net.Sockets.TcpClient]::new()
        try {
            $connect = $client.BeginConnect($parts.Host, $parts.Port, $null, $null)
            if ($connect.AsyncWaitHandle.WaitOne(200)) {
                $client.EndConnect($connect)
                return
            }
        } catch {
        } finally {
            $client.Close()
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for $Name at $Endpoint"
}

function Start-Server([string]$Name, [string]$Binary, [string[]]$Arguments) {
    $logPath = Join-Path $LogDir "$Name.log"
    $errorLogPath = Join-Path $LogDir "$Name.err.log"
    $process = Start-Process -FilePath $Binary -ArgumentList $Arguments -RedirectStandardOutput $logPath -RedirectStandardError $errorLogPath -PassThru
    $Processes.Add($process)
}

function Invoke-Checked([string]$FilePath, [string[]]$Arguments) {
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE"
    }
}

$Status = 1
try {
    Invoke-Checked $CTestBin @(
        "--test-dir", $BuildDir,
        "-R", "test_cpp_framework_sample_parity|test_cpp_framework_spot_runtime|test_cpp_framework_ActorGateway_actor_session_relay",
        "--output-on-failure"
    )

    $ports = Reserve-Endpoints 22
    $registryPubEndpoint = if ($env:BINGO_REGISTRY_PUB_ENDPOINT) { $env:BINGO_REGISTRY_PUB_ENDPOINT } else { "tcp://$($ports[0])" }
    $registryRouterEndpoint = if ($env:BINGO_REGISTRY_ROUTER_ENDPOINT) { $env:BINGO_REGISTRY_ROUTER_ENDPOINT } else { "tcp://$($ports[1])" }
    $apiAChannelEndpoint = if ($env:BINGO_API_A_CHANNEL_ENDPOINT) { $env:BINGO_API_A_CHANNEL_ENDPOINT } else { "tcp://$($ports[2])" }
    $playAChannelEndpoint = if ($env:BINGO_PLAY_A_CHANNEL_ENDPOINT) { $env:BINGO_PLAY_A_CHANNEL_ENDPOINT } else { "tcp://$($ports[3])" }
    $sessionASpotEndpoint = if ($env:BINGO_SESSION_A_SPOT_ENDPOINT) { $env:BINGO_SESSION_A_SPOT_ENDPOINT } else { "tcp://$($ports[4])" }
    $sessionARouterEndpoint = if ($env:BINGO_SESSION_A_ROUTER_ENDPOINT) { $env:BINGO_SESSION_A_ROUTER_ENDPOINT } else { "tcp://$($ports[5])" }
    $sessionBSpotEndpoint = if ($env:BINGO_SESSION_B_SPOT_ENDPOINT) { $env:BINGO_SESSION_B_SPOT_ENDPOINT } else { "tcp://$($ports[6])" }
    $sessionBRouterEndpoint = if ($env:BINGO_SESSION_B_ROUTER_ENDPOINT) { $env:BINGO_SESSION_B_ROUTER_ENDPOINT } else { "tcp://$($ports[7])" }
    $playBChannelEndpoint = if ($env:BINGO_PLAY_B_CHANNEL_ENDPOINT) { $env:BINGO_PLAY_B_CHANNEL_ENDPOINT } else { "tcp://$($ports[8])" }
    $playASpotEndpoint = if ($env:BINGO_PLAY_A_SPOT_ENDPOINT) { $env:BINGO_PLAY_A_SPOT_ENDPOINT } else { "tcp://$($ports[9])" }
    $playASpotRouterEndpoint = if ($env:BINGO_PLAY_A_SPOT_ROUTER_ENDPOINT) { $env:BINGO_PLAY_A_SPOT_ROUTER_ENDPOINT } else { "tcp://$($ports[10])" }
    $sessionAStreamEndpoint = if ($env:BINGO_SESSION_A_STREAM_ENDPOINT) { $env:BINGO_SESSION_A_STREAM_ENDPOINT } else { "tcp://$($ports[11])" }
    $sessionBStreamEndpoint = if ($env:BINGO_SESSION_B_STREAM_ENDPOINT) { $env:BINGO_SESSION_B_STREAM_ENDPOINT } else { "tcp://$($ports[12])" }
    $playBSpotEndpoint = if ($env:BINGO_PLAY_B_SPOT_ENDPOINT) { $env:BINGO_PLAY_B_SPOT_ENDPOINT } else { "tcp://$($ports[13])" }
    $playBSpotRouterEndpoint = if ($env:BINGO_PLAY_B_SPOT_ROUTER_ENDPOINT) { $env:BINGO_PLAY_B_SPOT_ROUTER_ENDPOINT } else { "tcp://$($ports[14])" }
    $apiBChannelEndpoint = if ($env:BINGO_API_B_CHANNEL_ENDPOINT) { $env:BINGO_API_B_CHANNEL_ENDPOINT } else { "tcp://$($ports[15])" }
    $apiAPlayRouteEndpoint = if ($env:BINGO_API_A_PLAY_ROUTE_ENDPOINT) { $env:BINGO_API_A_PLAY_ROUTE_ENDPOINT } else { "tcp://$($ports[17])" }
    $apiBPlayRouteEndpoint = if ($env:BINGO_API_B_PLAY_ROUTE_ENDPOINT) { $env:BINGO_API_B_PLAY_ROUTE_ENDPOINT } else { "tcp://$($ports[18])" }
    $sessionAPlayRouteEndpoint = if ($env:BINGO_SESSION_A_PLAY_ROUTE_ENDPOINT) { $env:BINGO_SESSION_A_PLAY_ROUTE_ENDPOINT } else { "tcp://$($ports[19])" }
    $sessionBPlayRouteEndpoint = if ($env:BINGO_SESSION_B_PLAY_ROUTE_ENDPOINT) { $env:BINGO_SESSION_B_PLAY_ROUTE_ENDPOINT } else { "tcp://$($ports[20])" }

    # The sample owns its Redis: always provision a dedicated, throwaway container
    # so room-allocation state stays isolated per run and never touches a developer's
    # local Redis. (BINGO_REDIS_ENDPOINT is intentionally derived here, not read.)
    $docker = Get-Command docker -ErrorAction SilentlyContinue
    if ($null -eq $docker) {
        throw "Docker is required to run the Bingo sample (it provisions a dedicated Redis container)."
    }
    $RedisContainer = "bingo-cpp-redis-$PID-$([Guid]::NewGuid().ToString('N'))"
    & docker run -d --rm --name $RedisContainer -p "127.0.0.1::6379" redis:7.2-alpine | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to start Redis container."
    }
    $redisPort = (& docker port $RedisContainer "6379/tcp") -replace '^.*:', ''
    $redisEndpoint = "127.0.0.1:$redisPort"
    Wait-Endpoint "redis" "tcp://$redisEndpoint"

    $topologyArgs = @(
        "--sample.topology.registryPubEndpoint=$registryPubEndpoint",
        "--sample.topology.registryRouterEndpoint=$registryRouterEndpoint",
        "--sample.topology.apiChannelEndpoint=$apiAChannelEndpoint",
        "--sample.topology.apiAChannelEndpoint=$apiAChannelEndpoint",
        "--sample.topology.apiBChannelEndpoint=$apiBChannelEndpoint",
        "--sample.topology.playChannelEndpoint=$playAChannelEndpoint",
        "--sample.topology.playAChannelEndpoint=$playAChannelEndpoint",
        "--sample.topology.playBChannelEndpoint=$playBChannelEndpoint",
        "--sample.topology.playARouteEndpoint=$apiAPlayRouteEndpoint",
        "--sample.topology.playBRouteEndpoint=$apiBPlayRouteEndpoint",
        "--sample.topology.sessionAPlayRouteEndpoint=$sessionAPlayRouteEndpoint",
        "--sample.topology.sessionBPlayRouteEndpoint=$sessionBPlayRouteEndpoint",
        "--sample.topology.playASpotEndpoint=$playASpotEndpoint",
        "--sample.topology.playBSpotEndpoint=$playBSpotEndpoint",
        "--sample.topology.playASpotRouterEndpoint=$playASpotRouterEndpoint",
        "--sample.topology.playBSpotRouterEndpoint=$playBSpotRouterEndpoint",
        "--sample.topology.sessionSpotEndpoint=$sessionASpotEndpoint",
        "--sample.topology.sessionRouterEndpoint=$sessionARouterEndpoint",
        "--sample.topology.sessionAStreamEndpoint=$sessionAStreamEndpoint",
        "--sample.topology.sessionBStreamEndpoint=$sessionBStreamEndpoint",
        "--sample.topology.redisEndpoint=$redisEndpoint",
        "--sample.topology.redisKeyPrefix=$RedisKeyPrefix"
    )
    $serverArgs = @("--sample.host.keepRunning", "true") + $topologyArgs

    Start-Server "registry" $RegistryBin $serverArgs
    Wait-Endpoint "registry-router" $registryRouterEndpoint

    Start-Server "api-a" $ApiBin ($serverArgs + @("--sample.topology.apiNode=a"))
    Wait-Endpoint "api-a" $apiAChannelEndpoint
    Start-Server "api-b" $ApiBin ($serverArgs + @("--sample.topology.apiNode=b"))
    Wait-Endpoint "api-b" $apiBChannelEndpoint

    Start-Server "session-a" $SessionBin ($serverArgs + @(
        "--sample.topology.sessionNode=a",
        "--sample.topology.sessionSpotEndpoint=$sessionASpotEndpoint",
        "--sample.topology.sessionRouterEndpoint=$sessionARouterEndpoint",
        "--sample.topology.streamEndpoint=$sessionAStreamEndpoint"
    ))
    Wait-Endpoint "session-a-router" $sessionARouterEndpoint
    Wait-Endpoint "session-a-stream" $sessionAStreamEndpoint
    Wait-Endpoint "session-a-play-route" $sessionAPlayRouteEndpoint

    Start-Server "session-b" $SessionBin ($serverArgs + @(
        "--sample.topology.sessionNode=b",
        "--sample.topology.sessionSpotEndpoint=$sessionBSpotEndpoint",
        "--sample.topology.sessionRouterEndpoint=$sessionBRouterEndpoint",
        "--sample.topology.streamEndpoint=$sessionBStreamEndpoint"
    ))
    Wait-Endpoint "session-b-router" $sessionBRouterEndpoint
    Wait-Endpoint "session-b-stream" $sessionBStreamEndpoint
    Wait-Endpoint "session-b-play-route" $sessionBPlayRouteEndpoint

    Start-Server "play-a" $PlayBin ($serverArgs + @("--sample.topology.playNode=a"))
    Wait-Endpoint "play-a" $playAChannelEndpoint
    Wait-Endpoint "play-a-spot-router" $playASpotRouterEndpoint
    Wait-Endpoint "play-a-spot-pub" $playASpotEndpoint
    Start-Server "play-b" $PlayBin ($serverArgs + @("--sample.topology.playNode=b"))
    Wait-Endpoint "play-b" $playBChannelEndpoint
    Wait-Endpoint "play-b-spot-router" $playBSpotRouterEndpoint
    Wait-Endpoint "play-b-spot-pub" $playBSpotEndpoint

    $settleSeconds = if ($env:BINGO_STARTUP_SETTLE_SECONDS) { [int]$env:BINGO_STARTUP_SETTLE_SECONDS } else { 2 }
    Start-Sleep -Seconds $settleSeconds

    $clientLog = Join-Path $LogDir "client.log"
    Invoke-Checked $ClientBin @(
        "--session-a-stream-endpoint", $sessionAStreamEndpoint,
        "--session-b-stream-endpoint", $sessionBStreamEndpoint
    ) *> $clientLog

    if (-not (Select-String -Path $clientLog -Pattern "bingo=completed" -Quiet)) {
        throw "Bingo C++ client did not write completion marker."
    }
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=Bingo" -Quiet)) {
        throw "Bingo C++ client did not write stream-inbound marker."
    }
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=Bingo .* seq=[0-9]" -Quiet)) {
        throw "Bingo C++ client did not write sequenced stream-inbound response marker."
    }
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=Bingo .* name=.*Notify" -Quiet)) {
        throw "Bingo C++ client did not write stream-inbound push marker."
    }

    $Status = 0
} finally {
    Cleanup $Status
}

Write-Host "bingo full client/server self-check completed"
