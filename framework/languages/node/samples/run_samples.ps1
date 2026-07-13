param(
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
    [string[]]$Samples
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$nodeRoot = Split-Path -Parent $scriptDir
$previousNodeTestContext = $env:NODE_TEST_CONTEXT

if (-not $IsWindows) {
    & bash (Join-Path $scriptDir "run_samples.sh") @Samples
    exit $LASTEXITCODE
}

Push-Location $nodeRoot
try {
    npm run build | Out-Null
    Remove-Item Env:NODE_TEST_CONTEXT -ErrorAction SilentlyContinue
    node --test `
        --test-name-pattern "ZLinkEntrySpotActivation destroyActor does not invoke Entry Spot lifecycle callbacks" `
        "test/contract/actor-manager.test.js"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Output "node actor lifecycle sample gate completed"
} finally {
    if ($null -ne $previousNodeTestContext) {
        $env:NODE_TEST_CONTEXT = $previousNodeTestContext
    }
    Pop-Location
}

$sampleScripts = @{
    "TicTacToe.Ts" = Join-Path $scriptDir "TicTacToe.Ts/run_sample.ps1"
    "Bingo.Ts" = Join-Path $scriptDir "Bingo.Ts/run_sample.ps1"
    "DeliveryDispatch.Ts" = Join-Path $scriptDir "DeliveryDispatch.Ts/run_sample.ps1"
    "SupportChat.Ts" = Join-Path $scriptDir "SupportChat.Ts/run_sample.ps1"
    "GameQuest.Ts" = Join-Path $scriptDir "GameQuest.Ts/run_sample.ps1"
    "ShoppingMall.Ts" = Join-Path $scriptDir "ShoppingMall.Ts/run_sample.ps1"
}
$defaultSamples = @("TicTacToe.Ts", "Bingo.Ts", "DeliveryDispatch.Ts", "SupportChat.Ts", "GameQuest.Ts", "ShoppingMall.Ts")
$selectedSamples = if ($Samples.Count -eq 0) { $defaultSamples } else { $Samples }
$powerShellExecutable = [Environment]::ProcessPath

function Invoke-Sample([string]$Script) {
    $stdout = [System.IO.Path]::GetTempFileName()
    $stderr = [System.IO.Path]::GetTempFileName()
    try {
        $process = Start-Process -FilePath $powerShellExecutable `
            -ArgumentList @('-NoProfile', '-File', $Script) `
            -Wait -PassThru -NoNewWindow `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $output = (Get-Content -Raw $stdout) + (Get-Content -Raw $stderr)
        return @{ Status = $process.ExitCode; Output = $output }
    }
    finally {
        Remove-Item -Force $stdout, $stderr -ErrorAction SilentlyContinue
    }
}

foreach ($sample in $selectedSamples) {
    if (-not $sampleScripts.ContainsKey($sample)) {
        throw "Unknown Node sample '$sample'. Known samples: $($defaultSamples -join ', ')"
    }
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        $result = Invoke-Sample $sampleScripts[$sample]
        $result.Output | Write-Host
        if ($result.Status -eq 0) { break }
        if ($result.Output -notmatch 'ZlinkBindException|BindException|Address already in use|EADDRINUSE|errno=98' -or $attempt -eq 3) {
            exit $result.Status
        }
        Write-Warning "Node sample transient bind failure; retrying $sample ($attempt/3)."
        Start-Sleep -Seconds 1
    }
}
