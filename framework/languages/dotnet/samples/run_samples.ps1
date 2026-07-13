$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "sample_runner.ps1")

$knownSamples = @("TicTacToe", "Bingo", "SupportChat", "ShoppingMall", "DeliveryDispatch", "GameQuest")
$scopes = @(
    "zlink-tictactoe-dotnet-redis",
    "zlink-bingo-dotnet-redis",
    "zlink-supportchat-dotnet-redis",
    "zlink-shoppingmall-dotnet-redis",
    "zlink-deliverydispatch-dotnet-redis",
    "zlink-gamequest-dotnet-redis"
)
foreach ($scope in $scopes) {
    Remove-SampleRedisScope $scope
}

$selected = if ($args.Count -gt 0) { @($args) } else { $knownSamples }
foreach ($sample in $selected) {
    if ($sample -notin $knownSamples) {
        throw "Unknown .NET sample '$sample'."
    }
    & (Join-Path $ScriptDir "$sample/run_sample.ps1")
    if ($LASTEXITCODE -ne 0) {
        throw "$sample sample runner failed with exit code $LASTEXITCODE."
    }
}
