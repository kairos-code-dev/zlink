using TicTacToe.SessionGateway;

var result = await SessionGatewaySampleScenario.RunAsync();

Console.WriteLine(
    $"auth={result.XAuthentication.PlayerId},{result.OAuthentication.PlayerId}");
Console.WriteLine(
    $"created={result.CreatedGame.GameId}, reconnect={result.XReconnectAuthentication.PlayerId}");
Console.WriteLine(
    $"players=X:{result.XJoin.State.XPlayerId}, O:{result.OJoin.State.OPlayerId}");
Console.WriteLine(
    $"final={result.FinalState.Board}, status={result.FinalState.Status}, winner={result.FinalState.Winner}");
Console.WriteLine(
    $"notifications=state:{result.StateNotifications.Count}, joined:{result.PlayerJoinedNotifications.Count}");
