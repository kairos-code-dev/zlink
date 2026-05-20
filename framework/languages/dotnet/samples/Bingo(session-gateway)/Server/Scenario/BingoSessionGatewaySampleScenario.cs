using Bingo.SessionGateway.Api;
using Bingo.SessionGateway.Client;
using Bingo.SessionGateway.Infrastructure;
using Bingo.SessionGateway.Infrastructure.Configuration;
using Bingo.SessionGateway.Play;
using Bingo.SessionGateway.Registry;
using Bingo.SessionGateway.Session;
using Bingo.SessionGateway.Shared.Configuration;

namespace Bingo.SessionGateway.Server.Scenario;

public static class BingoSessionGatewaySampleScenario
{
    public static async ValueTask<BingoSessionGatewayClientResult> RunAsync(
        CancellationToken cancellationToken = default)
    {
        var topology = SampleTopology.Create();
        var registryMetadata = new InMemoryRegistryDiscoveryMetadata();
        var sessionLocations = new RegistryActorSessionLocationStore(registryMetadata, "bingo");
        var playRoutes = new RegistryPlayRouteStore(registryMetadata, "bingo");
        var routePublisher = new RegistryPlayRoutePublisher(playRoutes, playRoutes, topology);
        await routePublisher.BindInitialActorPlayRoutesAsync(SampleNames.ActorIds, cancellationToken)
            .ConfigureAwait(false);

        using var registryHost = RegistryHostFactory.Build(topology);
        using var apiHost = ApiServerHostFactory.Build(topology);
        using var playHost = PlayServerHostFactory.Build(topology, sessionLocations, playRoutes);
        using var sessionHost = SessionServerHostFactory.Build(
            topology,
            topology.PrimarySession,
            sessionLocations,
            playRoutes);

        await registryHost.StartAsync(cancellationToken).ConfigureAwait(false);
        await apiHost.StartAsync(cancellationToken).ConfigureAwait(false);
        await playHost.StartAsync(cancellationToken).ConfigureAwait(false);
        await sessionHost.StartAsync(cancellationToken).ConfigureAwait(false);

        try
        {
            return await new BingoSessionGatewayClient()
                .RunAsync(new BingoSessionGatewayClientOptions(topology.StreamEndpoint), cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            await sessionHost.StopAsync(CancellationToken.None).ConfigureAwait(false);
            await playHost.StopAsync(CancellationToken.None).ConfigureAwait(false);
            await apiHost.StopAsync(CancellationToken.None).ConfigureAwait(false);
            await registryHost.StopAsync(CancellationToken.None).ConfigureAwait(false);
        }
    }
}
