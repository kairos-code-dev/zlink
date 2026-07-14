Set-StrictMode -Version Latest
. "$PSScriptRoot/../../redis-common.ps1"
$ErrorActionPreference = "Stop"

$SampleDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $SampleDir

$LogDir = Join-Path $SampleDir "build/sample-logs"
$FlowLogDir = Join-Path $SampleDir "logs"
$ConfigDir = Join-Path $SampleDir "build/sample-config"
New-Item -ItemType Directory -Force -Path $LogDir, $FlowLogDir, $ConfigDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $LogDir "*.log")
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $FlowLogDir "*.log")
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $ConfigDir "*.properties")

$Gradle = if ($IsWindows) { Join-Path $SampleDir "../../gradlew.bat" } else { Join-Path $SampleDir "../../gradlew" }
$Processes = New-Object System.Collections.Generic.List[System.Diagnostics.Process]
$RedisContainer = $null

function Print-Logs {
    param([int]$Status)
    if ($Status -eq 0) { return }
    Get-ChildItem -Path $LogDir -Filter "*.log" -ErrorAction SilentlyContinue | ForEach-Object {
        [Console]::Error.WriteLine("===== $($_.FullName) =====")
        Get-Content -Path $_.FullName -Tail 200 -ErrorAction SilentlyContinue | ForEach-Object {
            [Console]::Error.WriteLine($_)
        }
    }
}

function Get-ChildProcessIds {
    param([int]$ParentId)
    if ($IsWindows) {
        Get-CimInstance Win32_Process -Filter "ParentProcessId=$ParentId" | ForEach-Object {
            [int]$_.ProcessId
            Get-ChildProcessIds -ParentId ([int]$_.ProcessId)
        }
    } else {
        & pgrep -P $ParentId 2>$null | ForEach-Object {
            if ($_ -match '^\d+$') {
                [int]$_
                Get-ChildProcessIds -ParentId ([int]$_)
            }
        }
    }
}

function Stop-TrackedProcessTree {
    param([System.Diagnostics.Process]$Process)
    $children = @(Get-ChildProcessIds -ParentId $Process.Id)
    [array]::Reverse($children)
    foreach ($childId in $children) {
        Stop-Process -Id $childId -Force -ErrorAction SilentlyContinue
    }
    if (-not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
    }
}

function Cleanup {
    param([int]$Status)
    Print-Logs $Status
    for ($i = $Processes.Count - 1; $i -ge 0; $i--) {
        Stop-TrackedProcessTree -Process $Processes[$i]
    }
    if ($RedisContainer) {
        Remove-ZlinkSampleRedis $RedisContainer
    }
}

function Reserve-Endpoints {
    param([int]$Count)
    $listeners = New-Object System.Collections.Generic.List[System.Net.Sockets.TcpListener]
    $endpoints = New-Object System.Collections.Generic.List[string]
    try {
        while ($endpoints.Count -lt $Count) {
            $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse("127.0.0.1"), 0)
            $listener.Start()
            $listeners.Add($listener)
            $endpoints.Add("127.0.0.1:$($listener.LocalEndpoint.Port)")
        }
        return $endpoints.ToArray()
    } finally {
        foreach ($listener in $listeners) {
            $listener.Stop()
        }
    }
}

function Wait-Port {
    param([string]$HostName, [int]$Port, [int]$TimeoutSeconds = 60)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $client = [System.Net.Sockets.TcpClient]::new()
        try {
            $connect = $client.BeginConnect($HostName, $Port, $null, $null)
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
    throw "Timed out waiting for ${HostName}:$Port"
}

function Split-Endpoint {
    param([string]$Endpoint)
    $parts = $Endpoint.Split(":")
    return @{ Host = $parts[0]; Port = [int]$parts[1] }
}

function Invoke-Gradle {
    param([string[]]$Arguments)
    & $Gradle @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Gradle failed: $($Arguments -join ' ')"
    }
}

function Get-AppBin {
    param([string]$Project, [string]$Name)
    $binDir = Join-Path $SampleDir "$Project/build/install/$Name/bin"
    return Join-Path $binDir $(if ($IsWindows) { "$Name.bat" } else { $Name })
}

function Start-AppRole {
    param([string]$Project, [string]$Name, [string]$Config, [string]$LogName)
    $logPath = Join-Path $LogDir $LogName
    $errorLogPath = Join-Path $LogDir ($LogName + ".err.log")
    $process = Start-Process -FilePath (Get-AppBin $Project $Name) -ArgumentList @("--config", $Config) -WorkingDirectory $SampleDir -NoNewWindow -RedirectStandardOutput $logPath -RedirectStandardError $errorLogPath -PassThru
    $Processes.Add($process)
}

$Status = 1
try {
    $endpoints = Reserve-Endpoints 15
    $apiAChannel = Split-Endpoint $endpoints[0]
    $playAChannel = Split-Endpoint $endpoints[1]
    $sessionASpot = Split-Endpoint $endpoints[2]
    $sessionARouter = Split-Endpoint $endpoints[3]
    $playASpot = Split-Endpoint $endpoints[4]
    $playARouter = Split-Endpoint $endpoints[5]
    $sessionAStream = Split-Endpoint $endpoints[6]
    $apiBChannel = Split-Endpoint $endpoints[7]
    $playBChannel = Split-Endpoint $endpoints[8]
    $sessionBSpot = Split-Endpoint $endpoints[9]
    $sessionBRouter = Split-Endpoint $endpoints[10]
    $playBSpot = Split-Endpoint $endpoints[11]
    $playBRouter = Split-Endpoint $endpoints[12]
    $sessionBStream = Split-Endpoint $endpoints[13]

    $redis = Start-ZlinkSampleRedis "zlink-redis-java-sample-bingo"
    $RedisContainer = $redis.ContainerId
    $redisEndpoint = $redis.Endpoint
    $redis = Split-Endpoint $redisEndpoint
    Wait-Port $redis.Host $redis.Port
    $redisKeyPrefix = if ($env:BINGO_REDIS_KEY_PREFIX) { $env:BINGO_REDIS_KEY_PREFIX } else { "bingo:java:${PID}:$([Guid]::NewGuid().ToString('N')):" }
    $properties = @"
apiAChannelEndpoint=tcp://$($apiAChannel.Host):$($apiAChannel.Port)
apiBChannelEndpoint=tcp://$($apiBChannel.Host):$($apiBChannel.Port)
playAChannelEndpoint=tcp://$($playAChannel.Host):$($playAChannel.Port)
playBChannelEndpoint=tcp://$($playBChannel.Host):$($playBChannel.Port)
sessionASpotEndpoint=tcp://$($sessionASpot.Host):$($sessionASpot.Port)
sessionBSpotEndpoint=tcp://$($sessionBSpot.Host):$($sessionBSpot.Port)
sessionARouterEndpoint=tcp://$($sessionARouter.Host):$($sessionARouter.Port)
sessionBRouterEndpoint=tcp://$($sessionBRouter.Host):$($sessionBRouter.Port)
playASpotEndpoint=tcp://$($playASpot.Host):$($playASpot.Port)
playBSpotEndpoint=tcp://$($playBSpot.Host):$($playBSpot.Port)
playASpotRouterEndpoint=tcp://$($playARouter.Host):$($playARouter.Port)
playBSpotRouterEndpoint=tcp://$($playBRouter.Host):$($playBRouter.Port)
sessionAStreamEndpoint=tcp://$($sessionAStream.Host):$($sessionAStream.Port)
sessionBStreamEndpoint=tcp://$($sessionBStream.Host):$($sessionBStream.Port)
redisEndpoint=$redisEndpoint
redisKeyPrefix=$redisKeyPrefix
logDirectory=$($FlowLogDir.Replace('\', '/'))
"@
    function Write-SampleConfig {
        param([string]$Name, [string]$RoleName, [string]$RoleValue, [string]$PlayRid = "2202")
        $path = Join-Path $ConfigDir "$Name.properties"
        Set-Content -Path $path -Value "$properties`n$RoleName=$RoleValue`nplayRid=$PlayRid" -Encoding utf8NoBOM
        return $path
    }
    $sessionAConfig = Write-SampleConfig "session-a" "sessionNode" "a"
    $sessionBConfig = Write-SampleConfig "session-b" "sessionNode" "b"
    $apiAConfig = Write-SampleConfig "api-a" "apiNode" "a"
    $apiBConfig = Write-SampleConfig "api-b" "apiNode" "b"
    $playAConfig = Write-SampleConfig "play-a" "playNode" "a" "2201"
    $playBConfig = Write-SampleConfig "play-b" "playNode" "b" "2202"
    $clientConfig = Write-SampleConfig "client" "clientNode" "client"

    Push-Location "../../.."
    try {
        & ./gradlew --no-daemon :zlink-framework-core:jar :zlink-framework-spring-boot-starter:jar :zlink-framework-locations-redis:jar :zlink-framework-codec-protobuf:jar :zlink-stream-connector:jar --quiet
        if ($LASTEXITCODE -ne 0) { throw "Framework jar build failed" }
    } finally {
        Pop-Location
    }

    Invoke-Gradle @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:Session:installDist", ":Server:Api:installDist", ":Server:Play:installDist", ":Client:installDist", "--quiet")

    Start-AppRole "Server/Session" "Session" $sessionAConfig "session-a.log"
    Start-AppRole "Server/Session" "Session" $sessionBConfig "session-b.log"
    Start-AppRole "Server/Api" "Api" $apiAConfig "api-a.log"
    Start-AppRole "Server/Api" "Api" $apiBConfig "api-b.log"
    Start-AppRole "Server/Play" "Play" $playAConfig "play-a.log"
    Start-AppRole "Server/Play" "Play" $playBConfig "play-b.log"

    Wait-Port $sessionARouter.Host $sessionARouter.Port
    Wait-Port $sessionAStream.Host $sessionAStream.Port
    Wait-Port $sessionBRouter.Host $sessionBRouter.Port
    Wait-Port $sessionBStream.Host $sessionBStream.Port
    Wait-Port $apiAChannel.Host $apiAChannel.Port
    Wait-Port $apiBChannel.Host $apiBChannel.Port
    Wait-Port $playARouter.Host $playARouter.Port
    Wait-Port $playASpot.Host $playASpot.Port
    Wait-Port $playBRouter.Host $playBRouter.Port
    Wait-Port $playBSpot.Host $playBSpot.Port

    $clientLog = Join-Path $LogDir "client.log"
    & (Get-AppBin "Client" "Client") --config $clientConfig *> $clientLog
    if ($LASTEXITCODE -ne 0) {
        throw "Client run failed."
    }
    if (-not (Select-String -Path $clientLog -Pattern "bingo=completed" -Quiet)) {
        throw "Client completion marker was not found."
    }
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=Bingo" -Quiet)) {
        throw "Client inbound stream evidence was not found."
    }
    $Status = 0
} finally {
    Cleanup $Status
}
