$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$DotNetRoot = Split-Path -Parent $ScriptDir

dotnet test (Join-Path $DotNetRoot "tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj") `
    --filter "FullyQualifiedName~ActorLifecycleTests.EntrySpot_DestroyActorAsync_Removes_EntryOwned_Actor_Without_Left_Callback" `
    --logger "console;verbosity=minimal"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output "dotnet actor lifecycle sample gate completed"

& (Join-Path $ScriptDir "TicTacToe/run_sample.ps1")
& (Join-Path $ScriptDir "Bingo/run_sample.ps1")
& (Join-Path $ScriptDir "SupportChat/run_sample.ps1")
& (Join-Path $ScriptDir "ShoppingMall/run_sample.ps1")
& (Join-Path $ScriptDir "DeliveryDispatch/run_sample.ps1")
& (Join-Path $ScriptDir "GameQuest/run_sample.ps1")
