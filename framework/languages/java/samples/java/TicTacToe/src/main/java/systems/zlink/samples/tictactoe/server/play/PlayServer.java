package systems.zlink.samples.tictactoe.server.play;

import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.configuration.SampleTopology;
import systems.zlink.samples.tictactoe.server.play.actors.PlayActorFactory;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGame;
import systems.zlink.samples.tictactoe.server.play.sessions.PlaySession;

public final class PlayServer {
    private PlayServer() {
    }

    public static void configure(ZLinkFrameworkOptions options) {
        options.codecs().addJson();
        options.addHandlersFromPackageOf(PlayServer.class);
        options.addActorFactory(SampleNames.PlayActor, PlayActorFactory.class);
        options.addClientServerChannel(SampleNames.PlayChannel, channel -> {
            channel.enableServer(server -> server.bind(SampleTopology.PlayChannelEndpoint));
            channel.addHandlerGroup(SampleNames.PlayChannel);
        });
        options.addSpotMesh(SampleNames.SpotMesh, mesh ->
            mesh.addNode(SampleNames.PlayNode, node -> node.addSpotFactory(TicTacToeGame.class)));
        options.addStreamNode(SampleNames.PlayStream, stream -> {
            stream.bind(SampleTopology.PlayStreamEndpoint);
            stream.attachActorGateway(SampleNames.PlayNode);
            stream.registerSession(PlaySession.class);
        });
    }
}
