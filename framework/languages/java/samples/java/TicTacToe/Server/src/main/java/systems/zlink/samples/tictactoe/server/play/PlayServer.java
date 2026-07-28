package systems.zlink.samples.tictactoe.server.play;

import systems.zlink.framework.configuration.ClientServerChannelBuilder;
import systems.zlink.framework.configuration.ZLinkMeshNodeBuilder;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.tictactoe.server.configuration.SampleLogging;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.configuration.PlaySettings;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.actors.PlayActorFactory;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.actors.PlayActorRelocationAdapter;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.actors.PlayActor;
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

    public static ZLinkFrameworkConfigurer configure(PlaySettings settings) {
        return options -> {
            SampleLogging.configure(settings, "play");
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.DIAGNOSTIC)
                .traceLogFile(SampleLogging.flowLogPath(settings, "play-" + settings.playIndex()))
                .traceLabel("play-" + settings.playIndex());
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient(settings.apiChannelEndpoint());
            ClientServerChannelBuilder playChannel = options
                .addClientServerChannel(SampleNames.PlayChannel)
                .enableServer(settings.playChannelEndpoint());
            playChannel.addRequestHandler(
                CreateGameHandler.class,
                CreateGameReq.class,
                CreateGameRes.class);
            ZLinkMeshNodeBuilder node = options.addRouteMesh(SampleNames.SpotMesh);
            String routeEndpoint = settings.routeEndpoint().isBlank()
                ? settings.spotEndpoint()
                : settings.routeEndpoint();
            node.listen(routeEndpoint)
                .setRoutingIdPrefix("tictactoe-play");
            node.channelName(SampleNames.PlayNode);
            node.peerConnections().connect(settings.peerSpotEndpoint());
            node.objects()
                .server()
                .addEntrySpot(PlayEntrySpot.class)
                .addSpotFactory(
                    "tictactoe.game",
                    TicTacToeGame.class,
                    factory -> factory.disableRelocation())
                .addActorFactory(
                    SampleNames.PlayActor,
                    PlayActor.class,
                    PlayActorFactory.class,
                    factory -> factory.preserveStateWith(
                        PlayActorRelocationAdapter.class));
            options.addStreamNode(SampleNames.PlayStream)
                .bind(settings.playEndpoint())
                .enableActorDispatch(SampleNames.SpotMesh)
                .registerSession(PlaySession.class)
                .addSessionPacketHandler(AuthenticatePlaySessionHandler.class);
        };
    }
}
