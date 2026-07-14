package systems.zlink.samples.tictactoe.server.play;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.ClientServerChannelBuilder;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.tictactoe.server.configuration.SampleLogging;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.actors.PlayActorFactory;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.actors.PlayActorTransferAdapter;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.handlers.CreateGameHandler;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.entryspot.PlayEntrySpot;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.tictactoegamespot.TicTacToeGame;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.sessions.PlaySession;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.sessions.handlers.AuthenticatePlaySessionHandler;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameRes;

public final class PlayServer {
    private PlayServer() {
    }

    public static ZLinkFrameworkConfigurer configure(SampleSettings settings) {
        return options -> {
            SampleLogging.configure(settings, "play");
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.DIAGNOSTIC)
                .traceLogFile(SampleLogging.flowLogPath(settings, settings.playSpotNodeRid()))
                .traceLabel(settings.playSpotNodeRid());
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient(settings.apiChannelEndpoint());
            ClientServerChannelBuilder playChannel = options
                .addClientServerChannel(SampleNames.playChannel(settings.playIndex()))
                .enableServer(settings.playChannelEndpoint());
            playChannel.addRequestHandler(
                CreateGameHandler.class,
                CreateGameReq.class,
                CreateGameRes.class);
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.SpotMesh);
            String routeEndpoint = settings.routeEndpoint().isBlank()
                ? settings.spotEndpoint()
                : settings.routeEndpoint();
            node.enableRouter(routeEndpoint)
                .setRoutingId(RoutingId.from(settings.playSpotNodeRid()));
            node.connectRouter(RoutingId.from(settings.peerPlaySpotNodeRid()), settings.peerSpotEndpoint());
            node.enablePubSub(settings.spotPubSubEndpoint());
            node.connectPeerPub(settings.peerSpotPubSubEndpoint());
            node.configureEntrySpot().setRoutingId(RoutingId.from(SampleNames.EntrySpotRoutingId));
            node.addEntrySpot(PlayEntrySpot.class);
            node.addSpotFactory(TicTacToeGame.class);
            node.addActorFactory(SampleNames.PlayActor, PlayActorFactory.class);
            node.addActorTransferAdapter(SampleNames.PlayActor, PlayActorTransferAdapter.class);
            options.addStreamNode(SampleNames.PlayStream)
                .bind(settings.playEndpoint())
                .registerSession(PlaySession.class)
                .addSessionPacketHandler(AuthenticatePlaySessionHandler.class);
        };
    }
}
