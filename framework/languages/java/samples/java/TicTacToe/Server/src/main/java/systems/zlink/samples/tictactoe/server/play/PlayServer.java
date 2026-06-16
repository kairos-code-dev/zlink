package systems.zlink.samples.tictactoe.server.play;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.tictactoe.server.configuration.SampleLogging;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.play.adapters.zlink.actors.PlayActorFactory;
import systems.zlink.samples.tictactoe.server.play.adapters.zlink.spots.PlayEntrySpot;
import systems.zlink.samples.tictactoe.server.play.adapters.zlink.spots.TicTacToeGame;
import systems.zlink.samples.tictactoe.server.play.adapters.zlink.sessions.PlaySession;
import systems.zlink.samples.tictactoe.server.play.adapters.zlink.sessions.handlers.AuthenticatePlaySessionHandler;

public final class PlayServer {
    private PlayServer() {
    }

    public static ZLinkFrameworkConfigurer configure(SampleSettings settings) {
        return options -> {
            SampleLogging.configure(settings, "play");
            options.codecs().addMessagePack();
            options.addHandlersFromPackageOf(PlayServer.class);
            options.addActorFactory(SampleNames.PlayActor, PlayActorFactory.class);
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient(settings.apiChannelEndpoint());
            options.addClientServerChannel(SampleNames.PlayChannel)
                .enableServer(settings.playChannelEndpoint())
                .addHandlerGroup(SampleNames.PlayChannel);
            RouteMeshChannelBuilder route = options.addRouteMeshChannel(SampleNames.PlayRouteChannel);
            route.enableServer(settings.playRouterEndpoint());
            route.enableClient(settings.playRouterEndpoint());
            route.configureRouting().setRoutingId(RoutingId.from(SampleNames.PlayRouterId));
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.SpotMesh)
                .addNode(SampleNames.PlayNode);
            node.enableRouter(settings.spotEndpoint())
                .setRouterRoutingId(RoutingId.from(SampleNames.PlayNodeRoutingId));
            node.configureEntrySpot().setRoutingId(RoutingId.from(SampleNames.EntrySpotRoutingId));
            node.addEntrySpot(PlayEntrySpot.class);
            node.addSpotFactory(TicTacToeGame.class);
            options.addStreamNode(SampleNames.PlayStream)
                .bind(settings.playEndpoint())
                .registerSession(PlaySession.class)
                .addSessionPacketHandler(AuthenticatePlaySessionHandler.class);
        };
    }
}
