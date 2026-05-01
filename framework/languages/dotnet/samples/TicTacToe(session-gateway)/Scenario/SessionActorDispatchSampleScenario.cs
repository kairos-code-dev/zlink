using TicTacToe.SessionActorDispatch.Client;
using TicTacToe.SessionActorDispatch.Api;
using TicTacToe.SessionActorDispatch.Configuration;
using TicTacToe.SessionActorDispatch.Contracts;
using TicTacToe.SessionActorDispatch.Infrastructure;
using TicTacToe.SessionActorDispatch.Play;
using TicTacToe.SessionActorDispatch.Session;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Spots;

namespace TicTacToe.SessionActorDispatch;

public static class SessionActorDispatchSampleScenario
{
    public static async ValueTask<SessionActorDispatchSampleResult> RunAsync(
        CancellationToken cancellationToken = default)
    {
        var topology = SampleTopology.Create();
        var registryMetadata = new InMemoryRegistryDiscoveryMetadata();
        var sessionLocations = new RegistryActorSessionLocationStore(registryMetadata, "tictactoe");
        var playRoutes = new RegistryPlayRouteStore(registryMetadata, "tictactoe");
        var routePublisher = new RegistryPlayRoutePublisher(playRoutes, topology);
        await routePublisher.BindInitialActorPlayRoutesAsync(
                new[] { SampleNames.XActorId, SampleNames.OActorId },
                cancellationToken)
            .ConfigureAwait(false);
        using var registryHost = RegistryHostFactory.Build(topology);
        using var apiHost = ApiServerHostFactory.Build(topology);
        using var playHost = PlayServerHostFactory.Build(topology, sessionLocations, playRoutes);
        using var sessionHost = SessionServerHostFactory.Build(
            topology,
            topology.PrimarySession,
            sessionLocations,
            playRoutes);
        using var reconnectSessionHost = SessionServerHostFactory.Build(
            topology,
            topology.ReconnectSession,
            sessionLocations,
            playRoutes);

        await registryHost.StartAsync(cancellationToken);
        await apiHost.StartAsync(cancellationToken);
        await playHost.StartAsync(cancellationToken);
        await sessionHost.StartAsync(cancellationToken);
        await reconnectSessionHost.StartAsync(cancellationToken);

        try
        {
            await using var xClient = await SessionActorDispatchPlayerClient.ConnectAsync(
                SampleNames.XActorId,
                topology.StreamEndpoint,
                cancellationToken);
            await using var oClient = await SessionActorDispatchPlayerClient.ConnectAsync(
                SampleNames.OActorId,
                topology.ReconnectStreamEndpoint,
                cancellationToken);

            var xAuth = await xClient.AuthenticateAsync(cancellationToken);
            var oAuth = await oClient.AuthenticateAsync(cancellationToken);
            var created = await xClient.CreateMatchAsync(cancellationToken);
            SessionActorDispatchSampleAssertions.ValidateCreatedMatch(created);
            await SessionActorDispatchSampleAssertions.ValidateGameRoomSpotAsync(
                    playHost.Services.GetRequiredService<IZLinkSpotManager>(),
                    created.MatchId,
                    cancellationToken)
                .ConfigureAwait(false);

            await xClient.DisposeAsync();
            await using var reconnectedXClient = await SessionActorDispatchPlayerClient.ConnectAsync(
                SampleNames.XActorId,
                topology.ReconnectStreamEndpoint,
                cancellationToken);
            var xReconnectAuth = await reconnectedXClient.AuthenticateAsync(cancellationToken);
            var xJoin = await reconnectedXClient.JoinAsync(created.MatchId, cancellationToken);
            var oJoin = await oClient.JoinAsync(created.MatchId, cancellationToken);
            var moves = new[]
            {
                await reconnectedXClient.PlaceMarkAsync(created.MatchId, 0, cancellationToken),
                await oClient.PlaceMarkAsync(created.MatchId, 3, cancellationToken),
                await reconnectedXClient.PlaceMarkAsync(created.MatchId, 1, cancellationToken),
                await oClient.PlaceMarkAsync(created.MatchId, 4, cancellationToken),
                await reconnectedXClient.PlaceMarkAsync(created.MatchId, 2, cancellationToken),
            };

            SessionActorDispatchSampleAssertions.ValidateReconnect(xAuth, xReconnectAuth, xJoin.State);
            SessionActorDispatchSampleAssertions.ValidateFinalState(moves[^1].State);

            await SessionActorDispatchSampleAssertions.WaitForNotificationsAsync(
                    reconnectedXClient,
                    oClient,
                    cancellationToken)
                .ConfigureAwait(false);
            SessionActorDispatchSampleAssertions.ValidateNotifications(reconnectedXClient, oClient);
            return new SessionActorDispatchSampleResult(
                xAuth,
                oAuth,
                xReconnectAuth,
                created,
                xJoin,
                oJoin,
                moves,
                reconnectedXClient.TurnChangedNotifications.Concat(oClient.TurnChangedNotifications).ToArray(),
                reconnectedXClient.OpponentJoinedNotifications.Concat(oClient.OpponentJoinedNotifications).ToArray(),
                reconnectedXClient.GameEndedNotifications.Concat(oClient.GameEndedNotifications).ToArray());
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
}

public sealed record SessionActorDispatchSampleResult(
    AuthenticateRes XAuthentication,
    AuthenticateRes OAuthentication,
    AuthenticateRes XReconnectAuthentication,
    CreateMatchRes CreatedMatch,
    JoinMatchRes XJoin,
    JoinMatchRes OJoin,
    IReadOnlyList<PlaceMarkRes> Moves,
    IReadOnlyList<TurnChangedNotify> TurnChangedNotifications,
    IReadOnlyList<OpponentJoinedNotify> OpponentJoinedNotifications,
    IReadOnlyList<GameEndedNotify> GameEndedNotifications)
{
    public TicTacToeState FinalState => Moves[^1].State;
}
