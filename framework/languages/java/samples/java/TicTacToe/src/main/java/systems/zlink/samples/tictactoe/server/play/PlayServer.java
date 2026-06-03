package systems.zlink.samples.tictactoe.server.play;

import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.configuration.SampleTopology;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGame;
import systems.zlink.samples.tictactoe.server.play.sessions.PlaySession;

public final class PlayServer {
    private PlayServer() {
    }

    public static void configure(ZLinkFrameworkOptions options) {
        options.addHandlersFromPackageOf(PlayServer.class);
        options.addClientServerChannel(SampleNames.PlayChannel, channel -> {
            channel.enableServer(server -> server.bind(SampleTopology.PlayChannelEndpoint));
            channel.addHandlerGroup("play");
        });
        options.addSpotMesh(SampleNames.SpotMesh, mesh ->
            mesh.addNode(SampleNames.PlayNode, node -> node.addSpotFactory(TicTacToeGame.class)));
        options.addStreamNode(SampleNames.PlayStream, stream -> {
            stream.bind(SampleTopology.PlayStreamEndpoint);
            stream.registerSession(PlaySession.class);
        });
    }
}
