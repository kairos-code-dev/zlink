using Microsoft.Extensions.DependencyInjection;
using TicTacToe.SessionActorDispatch;
using TicTacToe.SessionActorDispatch.Infrastructure;
using TicTacToe.SessionActorDispatch.Session;
using TicTacToe.SessionGateway.Api;
using TicTacToe.SessionGateway.Client;
using TicTacToe.SessionGateway.Infrastructure;
using TicTacToe.SessionGateway.Infrastructure.Configuration;
using TicTacToe.SessionGateway.Play;
using TicTacToe.SessionGateway.Registry;
using TicTacToe.SessionGateway.Shared.Configuration;
using Zlink.Framework.Spots;

namespace TicTacToe.SessionGateway.Server.Scenario;

public static class SessionActorDispatchSampleScenario
{
    public static async ValueTask<SessionActorDispatchClientResult> RunAsync(
        CancellationToken cancellationToken = default)
    {
        var topology = SampleTopology.Create();
        var registryMetadata = new InMemoryRegistryDiscoveryMetadata();
        var sessionLocations = new RegistryActorSessionLocationStore(registryMetadata, "tictactoe");
        var playRoutes = new RegistryPlayRouteStore(registryMetadata, "tictactoe");
        var routePublisher = new RegistryPlayRoutePublisher(playRoutes, playRoutes, topology);
        await routePublisher.BindInitialActorPlayRoutesAsync(
                [SampleNames.XActorId, SampleNames.OActorId],
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
            var client = new SessionActorDispatchClient();
            var result = await client.RunAsync(
                    new SessionActorDispatchClientOptions(
                        topology.StreamEndpoint,
                        topology.ReconnectStreamEndpoint),
                    cancellationToken)
                .ConfigureAwait(false);
            SessionActorDispatchSampleAssertions.ValidateCreatedMatch(result.CreatedMatch);
            await SessionActorDispatchSampleAssertions.ValidateGameRoomSpotAsync(
                    playHost.Services.GetRequiredService<IZLinkSpotManager>(),
                    result.CreatedMatch.MatchId,
                    cancellationToken)
                .ConfigureAwait(false);

            return result;
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
