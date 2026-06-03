package systems.zlink.samples.tictactoe.sessiongateway.server.play;

import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.entryspot.TicTacToeEntrySpot;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameSpot;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.handlers.PlaceMarkHandler;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.entryspot.handlers.JoinMatchHandler;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.handlers.CreateMatchRoomHandler;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.handlers.EnsurePlayerActorHandler;
import systems.zlink.samples.tictactoe.sessiongateway.shared.actors.PlayerActorFactory;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;

public final class PlayServer {
    private PlayServer() {
    }

    public static void configure(ZLinkFrameworkOptions options) {
        options.addClientServerChannel(SampleNames.PlayChannel, channel -> {
            channel.enableServer(server -> server.bind(SampleTopology.PlayEndpoint));
            channel.addRequestHandler(
                EnsurePlayerActorHandler.class,
                String.class,
                String.class,
                "EnsurePlayerActor");
            channel.addRequestHandler(
                CreateMatchRoomHandler.class,
                String.class,
                String.class,
                "CreateMatchRoom");
            channel.addRequestHandler(
                JoinMatchHandler.class,
                String.class,
                String.class,
                "JoinMatchReq");
            channel.addRequestHandler(
                PlaceMarkHandler.class,
                String.class,
                String.class,
                "PlaceMarkReq");
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

    public static void configureSessionRelayNode(ZLinkFrameworkOptions options) {
        options.addRouteMeshChannel(SampleNames.PlayRouteChannel, route -> {
            route.bind(SampleTopology.PlayRouteEndpoint);
            route.useManualConnections(endpoints ->
                endpoints.connect(SampleTopology.PlayRouteEndpoint));
        });
        options.addSpotMesh(SampleNames.SpotMesh, mesh -> {
            mesh.addNode(SampleNames.SessionRelayNode, node -> {
                node.enableRouter();
                node.addSpotFactory(SessionRelaySpot.class);
            });
        });
        options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory.class);
    }
}
