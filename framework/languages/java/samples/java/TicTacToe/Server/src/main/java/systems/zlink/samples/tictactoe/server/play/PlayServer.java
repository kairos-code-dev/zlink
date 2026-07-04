package systems.zlink.samples.tictactoe.server.play;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.tictactoe.server.configuration.SampleLogging;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.actors.PlayActorFactory;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.entryspot.PlayEntrySpot;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.tictactoegamespot.TicTacToeGame;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.sessions.PlaySession;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.sessions.handlers.AuthenticatePlaySessionHandler;

public final class PlayServer {
    private PlayServer() {
    }

    public static ZLinkFrameworkConfigurer configure(SampleSettings settings) {
        return options -> {
            SampleLogging.configure(settings, "play");
            options.codecs().use(ZLinkMessagePackCodec.defaultCodec());
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(SampleLogging.flowLogPath(settings.playSpotNodeRid()))
                .traceLabel(settings.playSpotNodeRid());
            options.configureLocations()
                .setSpotRouterChannel(SampleNames.SpotMesh, SampleNames.RouteChannel);
            options.addHandlersFromPackageOf(PlayServer.class);
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient(settings.apiChannelEndpoint());
            options.addClientServerChannel(SampleNames.PlayChannel)
                .enableServer(settings.playChannelEndpoint())
                .addHandlerGroup(SampleNames.PlayChannel);
            RouteMeshChannelBuilder route = options.addRouteMeshChannel(SampleNames.RouteChannel);
            route.enableServer(settings.routeEndpoint())
                .enableClient(settings.peerRouteEndpoint())
                .setRoutingId(RoutingId.from(settings.playSpotNodeRid()));
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.SpotMesh);
            node.enableRouter(settings.spotEndpoint())
                .setRoutingId(RoutingId.from(settings.playSpotNodeRid()));
            node.connectRouter(RoutingId.from(settings.peerPlaySpotNodeRid()), settings.peerSpotEndpoint());
            node.enablePubSub(settings.spotPubSubEndpoint());
            node.connectPeerPub(settings.peerSpotPubSubEndpoint());
            node.configureEntrySpot().setRoutingId(RoutingId.from(SampleNames.EntrySpotRoutingId));
            node.addEntrySpot(PlayEntrySpot.class);
            node.addSpotFactory(TicTacToeGame.class);
            node.addActorFactory(SampleNames.PlayActor, PlayActorFactory.class);
            options.addStreamNode(SampleNames.PlayStream)
                .bind(settings.playEndpoint())
                .registerSession(PlaySession.class)
                .addSessionPacketHandler(AuthenticatePlaySessionHandler.class);
        };
    }
}
