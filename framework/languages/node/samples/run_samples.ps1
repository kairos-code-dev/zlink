Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$nodeRoot = Split-Path -Parent $scriptDir
$previousNodeTestContext = $env:NODE_TEST_CONTEXT

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

& (Join-Path $scriptDir "TicTacToe.Ts/run_sample.ps1")
& (Join-Path $scriptDir "Bingo.Ts/run_sample.ps1")
& (Join-Path $scriptDir "DeliveryDispatch.Ts/run_sample.ps1")
& (Join-Path $scriptDir "GameQuest.Ts/run_sample.ps1")
& (Join-Path $scriptDir "ShoppingMall.Ts/run_sample.ps1")
& (Join-Path $scriptDir "ShoppingMallCheckout.Ts/run_sample.ps1")
& (Join-Path $scriptDir "SupportChat.Ts/run_sample.ps1")
