namespace Bingo.SessionGateway;

internal static class BingoScenario
{
    public static async ValueTask<BingoScenarioResult> RunAsync(CancellationToken cancellationToken = default)
    {
        var playServer = new PlayServer();
        var apiServer = new ApiServer(playServer);
        var sessionServer = new SessionServer(apiServer, playServer);
        var clients = Enumerable.Range(1, BingoRoomSpot.RequiredPlayers)
            .Select(_ => new BingoClientConnector(sessionServer))
            .ToArray();

        var auth = clients
            .Select((client, index) => client.Authenticate($"player-{index + 1}"))
            .ToArray();

        var firstMatch = clients[0].Match("four-player");
        Exception? earlyHostStartError = null;
        try
        {
            await clients[0].StartAsync(firstMatch.RoomId, cancellationToken).ConfigureAwait(false);
        }
        catch (InvalidOperationException ex)
        {
            earlyHostStartError = ex;
        }

        var matches = new[] { firstMatch }
            .Concat(clients.Skip(1).Select(client => client.Match("four-player")))
            .ToArray();

        Exception? nonHostStartError = null;
        try
        {
            await clients[1].StartAsync(firstMatch.RoomId, cancellationToken).ConfigureAwait(false);
        }
        catch (InvalidOperationException ex)
        {
            nonHostStartError = ex;
        }

        var started = await clients[0]
            .StartAsync(firstMatch.RoomId, cancellationToken)
            .ConfigureAwait(false);
        var finalState = playServer.FindRoom(firstMatch.RoomId).Snapshot();
        var result = new BingoScenarioResult(
            auth,
            matches,
            started,
            finalState,
            clients.Select(client => client.Pushes.ToArray()).ToArray(),
            earlyHostStartError,
            nonHostStartError);

        BingoScenarioAssertions.Validate(result);
        return result;
    }
}

internal sealed record BingoScenarioResult(
    IReadOnlyList<AuthenticateRes> Authentications,
    IReadOnlyList<MatchBingoRes> Matches,
    StartBingoGameRes Started,
    BingoRoomState FinalState,
    IReadOnlyList<IReadOnlyList<object>> ClientPushes,
    Exception? EarlyHostStartError,
    Exception? NonHostStartError);
