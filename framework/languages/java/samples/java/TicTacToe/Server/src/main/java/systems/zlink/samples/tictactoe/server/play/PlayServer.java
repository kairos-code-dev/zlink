package systems.zlink.samples.tictactoe.server.play;

import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.samples.tictactoe.server.configuration.SampleLogging;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.play.actors.PlayActorFactory;
import systems.zlink.samples.tictactoe.server.play.entryspot.PlayEntrySpot;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGame;
import systems.zlink.samples.tictactoe.server.play.sessions.PlaySession;

public final class PlayServer {
    private PlayServer() {
    }

    public static void configure(ZLinkFrameworkOptions options) {
        configure(options, SampleSettings.current());
    }

    public static void configure(ZLinkFrameworkOptions options, SampleSettings settings) {
        SampleSettings.setCurrent(settings);
        SampleLogging.configure(settings, "play");
        options.codecs().addJson();
        options.addHandlersFromPackageOf(PlayServer.class);
        options.addActorFactory(SampleNames.PlayActor, PlayActorFactory.class);
        options.addClientServerChannel(SampleNames.ApiChannel, channel ->
            channel.enableClient(client -> client.useManualConnections(
                endpoints -> endpoints.connect(settings.apiChannelEndpoint()))));
        options.addClientServerChannel(SampleNames.PlayChannel, channel -> {
            channel.enableClient(client -> client.useManualConnections(
                endpoints -> endpoints.connect(settings.playChannelEndpoint())));
            channel.enableServer(server -> server.bind(settings.playChannelEndpoint()));
            channel.addHandlerGroup(SampleNames.PlayChannel);
        });
        options.addSpotMesh(SampleNames.SpotMesh, mesh ->
            mesh.addNode(SampleNames.PlayNode, node -> {
                node.enableRouter(router -> {
                    router.setRoutingId(RoutingId.from(SampleNames.PlayNodeRoutingId));
                    router.setRouterBind(settings.playRouterEndpoint());
                    router.useManualConnections(endpoints ->
                        endpoints.connect(settings.playRouterEndpoint()));
                });
                node.configureEntrySpot(entry ->
                    entry.setRoutingId(RoutingId.from(SampleNames.EntrySpotRoutingId)));
                node.addEntrySpot(PlayEntrySpot.class);
                node.addSpotFactory(TicTacToeGame.class);
            }));
        options.addStreamNode(SampleNames.PlayStream, stream -> {
            stream.bind(settings.playEndpoint());
            stream.attachActorGateway(SampleNames.PlayNode);
            stream.registerSession(PlaySession.class);
        });
    }
}
