using TicTacToe.SessionGateway.Client;
using TicTacToe.SessionGateway.Configuration;
using TicTacToe.SessionGateway.Contracts;
using TicTacToe.SessionGateway.Api;
using TicTacToe.SessionGateway.Infrastructure;
using TicTacToe.SessionGateway.Play;
using TicTacToe.SessionGateway.Session;

namespace TicTacToe.SessionGateway;

public static class SessionGatewaySampleScenario
{
    public static async ValueTask<SessionGatewaySampleResult> RunAsync(
        CancellationToken cancellationToken = default)
    {
        var topology = SampleTopology.Create();
        using var registryHost = RegistryHostFactory.Build(topology);
        using var apiHost = ApiServerHostFactory.Build(topology);
        using var playHost = PlayServerHostFactory.Build(topology);
        using var sessionHost = SessionServerHostFactory.Build(topology, topology.PrimarySession);
        using var reconnectSessionHost = SessionServerHostFactory.Build(topology, topology.ReconnectSession);

        await registryHost.StartAsync(cancellationToken);
        await apiHost.StartAsync(cancellationToken);
        await playHost.StartAsync(cancellationToken);
        await sessionHost.StartAsync(cancellationToken);
        await reconnectSessionHost.StartAsync(cancellationToken);

        try
        {
            await using var xClient = await SessionGatewayPlayerClient.ConnectAsync(
                SampleNames.XPlayerId,
                topology.StreamEndpoint,
                cancellationToken);
            await using var oClient = await SessionGatewayPlayerClient.ConnectAsync(
                SampleNames.OPlayerId,
                topology.ReconnectStreamEndpoint,
                cancellationToken);

            var xAuth = await xClient.AuthenticateAsync(cancellationToken);
            var oAuth = await oClient.AuthenticateAsync(cancellationToken);
            var created = await xClient.CreateGameAsync(SampleNames.GameId, cancellationToken);

            await xClient.DisposeAsync();
            await using var reconnectedXClient = await SessionGatewayPlayerClient.ConnectAsync(
                SampleNames.XPlayerId,
                topology.ReconnectStreamEndpoint,
                cancellationToken);
            var xReconnectAuth = await reconnectedXClient.AuthenticateAsync(cancellationToken);
            var xJoin = await reconnectedXClient.JoinAsync(created.GameId, cancellationToken);
            var oJoin = await oClient.JoinAsync(created.GameId, cancellationToken);
            var moves = new[]
            {
                await reconnectedXClient.PlaceMarkAsync(created.GameId, 0, cancellationToken),
                await oClient.PlaceMarkAsync(created.GameId, 3, cancellationToken),
                await reconnectedXClient.PlaceMarkAsync(created.GameId, 1, cancellationToken),
                await oClient.PlaceMarkAsync(created.GameId, 4, cancellationToken),
                await reconnectedXClient.PlaceMarkAsync(created.GameId, 2, cancellationToken),
            };

            ValidateReconnect(xAuth, xReconnectAuth, xJoin.State);
            ValidateFinalState(moves[^1].State);

            await WaitForNotificationsAsync(reconnectedXClient, oClient, cancellationToken);
            return new SessionGatewaySampleResult(
                xAuth,
                oAuth,
                xReconnectAuth,
                created,
                xJoin,
                oJoin,
                moves,
                reconnectedXClient.StateNotifications.Concat(oClient.StateNotifications).ToArray(),
                reconnectedXClient.PlayerJoinedNotifications.Concat(oClient.PlayerJoinedNotifications).ToArray());
        }
        finally
        {
            await reconnectSessionHost.StopAsync(CancellationToken.None);
            await sessionHost.StopAsync(CancellationToken.None);
            await playHost.StopAsync(CancellationToken.None);
            await apiHost.StopAsync(CancellationToken.None);
            await registryHost.StopAsync(CancellationToken.None);
        }
    }

    private static void ValidateFinalState(GameState state)
    {
        if (!string.Equals(state.Status, "Won", StringComparison.Ordinal)
            || !string.Equals(state.Winner, SampleNames.XPlayerId, StringComparison.Ordinal)
            || !string.Equals(state.Board, "XXXOO....", StringComparison.Ordinal))
        {
            throw new InvalidOperationException(
                $"Unexpected final state: board={state.Board}, status={state.Status}, winner={state.Winner ?? "-"}");
        }
    }

    private static void ValidateReconnect(
        AuthenticateRes firstAuth,
        AuthenticateRes reconnectAuth,
        GameState reconnectedJoinState)
    {
        if (!string.Equals(firstAuth.PlayerId, SampleNames.XPlayerId, StringComparison.Ordinal)
            || !string.Equals(reconnectAuth.PlayerId, SampleNames.XPlayerId, StringComparison.Ordinal)
            || !string.Equals(reconnectedJoinState.XPlayerId, SampleNames.XPlayerId, StringComparison.Ordinal))
        {
            throw new InvalidOperationException("Reconnect did not recover the same player binding.");
        }
    }


    private static async ValueTask WaitForNotificationsAsync(
        SessionGatewayPlayerClient xClient,
        SessionGatewayPlayerClient oClient,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + SampleTimings.RequestTimeout;
        while (DateTimeOffset.UtcNow < deadline)
        {
            if (xClient.StateNotifications.Count >= 7
                && oClient.StateNotifications.Count >= 6
                && xClient.PlayerJoinedNotifications.Count >= 1)
            {
                return;
            }

            await Task.Delay(TimeSpan.FromMilliseconds(20), cancellationToken);
        }

        throw new InvalidOperationException(
            "Timed out waiting for session-gateway notifications to reach both players.");
    }

}

public sealed record SessionGatewaySampleResult(
    AuthenticateRes XAuthentication,
    AuthenticateRes OAuthentication,
    AuthenticateRes XReconnectAuthentication,
    CreateGameRes CreatedGame,
    JoinGameRes XJoin,
    JoinGameRes OJoin,
    IReadOnlyList<PlaceMarkRes> Moves,
    IReadOnlyList<GameStateNotify> StateNotifications,
    IReadOnlyList<PlayerJoinedNotify> PlayerJoinedNotifications)
{
    public GameState FinalState => Moves[^1].State;
}
