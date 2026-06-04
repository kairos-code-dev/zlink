package systems.zlink.samples.tictactoe.sessiongateway.server.play;

import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.entryspot.TicTacToeEntrySpot;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameSpot;
import systems.zlink.samples.tictactoe.sessiongateway.shared.actors.PlayerActorFactory;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;

public final class PlayServer {
    private PlayServer() {
    }

    public static void configure(ZLinkFrameworkOptions options) {
        options.addHandlersFromPackageOf(PlayServer.class);
        options.addClientServerChannel(SampleNames.PlayChannel, channel -> {
            channel.enableServer(server -> server.bind(SampleTopology.PlayEndpoint));
            channel.addHandlerGroup("play");
        });
        options.addRouteMeshChannel(SampleNames.PlayRouteChannel, route -> {
            route.bind(SampleTopology.PlayRouteEndpoint);
            route.useManualConnections(endpoints ->
                endpoints.connect(SampleTopology.PlayRouteEndpoint));
        });
        options.useRegistrySpotRemoteAddresses(SampleNames.SpotMesh, registry ->
            registry.setRouterChannelId(SampleNames.PlayRouteChannel));
        options.addSpotMesh(SampleNames.SpotMesh, mesh -> {
            mesh.addNode(SampleNames.PlayNode, node -> {
                node.enableRouter();
                node.configureEntrySpot(entry ->
                    entry.setRoutingId(systems.zlink.contracts.core.RoutingId.from(
                        SampleNames.EntrySpotRoutingId)));
                node.addEntrySpot(TicTacToeEntrySpot.class);
                node.addSpotFactory(TicTacToeGameSpot.class);
            });
        });
        options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory.class);
    }

}
