package systems.zlink.samples.tictactoe.sessiongateway.server.play;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.entryspot.TicTacToeEntrySpot;
import systems.zlink.samples.tictactoe.sessiongateway.shared.actors.PlayerActorFactory;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;

public final class PlayServer {
    private PlayServer() {
    }

    public static void configure(ZLinkFrameworkOptions options) {
        options.addHandlersFromPackageOf(PlayServer.class);
        options.addClientServerChannel(SampleNames.PlayChannel, channel -> {
            channel.enableServer(server -> server.bind(SampleTopology.PlayChannelEndpoint));
            channel.addHandlerGroup("play");
        });
        options.addRouteMeshChannel(SampleNames.PlayRouteChannel, route -> {
            route.bind(SampleTopology.PlayRouteEndpoint);
            route.configureRouting(routing ->
                routing.setRoutingId(RoutingId.from(SampleNames.PlayRid)));
            route.useManualConnections(endpoints -> {
                endpoints.connect(SampleTopology.SessionRouteEndpoint);
                endpoints.connect(SampleTopology.ReconnectSessionRouteEndpoint);
            });
        });
        options.useRegistrySpotRemoteAddresses(SampleNames.SpotMesh, registry ->
            registry.setRouterChannelId(SampleNames.PlayRouteChannel));
        options.addSpotMesh(SampleNames.SpotMesh, mesh -> {
            mesh.addNode(SampleNames.PlayNode, node -> {
                node.enableRouter(router -> {
                    router.bindRouter(SampleTopology.PlaySpotRouterEndpoint);
                    router.setRoutingId(RoutingId.from(SampleNames.PlayRid));
                });
                node.acceptSpotRoutesFromChannel(SampleNames.PlayRouteChannel);
                node.configureEntrySpot(entry ->
                    entry.setRoutingId(RoutingId.from(
                        SampleNames.EntrySpotRoutingId)));
                node.addEntrySpot(TicTacToeEntrySpot.class);
            });
        });
        options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory.class);
    }

}
