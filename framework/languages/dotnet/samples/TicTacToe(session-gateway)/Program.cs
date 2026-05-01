using TicTacToe.SessionActorDispatch;

var result = await SessionActorDispatchSampleScenario.RunAsync();

Console.WriteLine(
    $"auth={result.XAuthentication.ActorId},{result.OAuthentication.ActorId}");
Console.WriteLine(
    $"created={result.CreatedMatch.MatchId}, reconnect={result.XReconnectAuthentication.ActorId}");
Console.WriteLine(
    $"players=X:{result.XJoin.State.XActorId}, O:{result.OJoin.State.OActorId}");
Console.WriteLine(
    $"final={result.FinalState.Board}, status={result.FinalState.Status}, winner={result.FinalState.WinnerActorId}");
Console.WriteLine(
    $"notifications=turn:{result.TurnChangedNotifications.Count}, joined:{result.OpponentJoinedNotifications.Count}, ended:{result.GameEndedNotifications.Count}");
