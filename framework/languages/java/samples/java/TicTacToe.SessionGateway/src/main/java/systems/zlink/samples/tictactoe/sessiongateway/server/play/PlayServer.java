package systems.zlink.samples.tictactoe.sessiongateway.server.play;

import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameSpot;
import systems.zlink.samples.tictactoe.sessiongateway.shared.actors.PlayerActorFactory;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;

public final class PlayServer {
    private PlayServer() {
    }

    public static void configure(ZLinkFrameworkOptions options) {
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
                node.addSpotFactory(TicTacToeGameSpot.class);
            });
            mesh.addNode(SampleNames.SessionRelayNode, node -> {
                node.enableRouter();
                node.addSpotFactory(SessionRelaySpot.class);
            });
        });
        options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory.class);
    }
}
