package systems.zlink.samples.tictactoe.sessiongateway.server.play;

import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameSpot;
import systems.zlink.samples.tictactoe.sessiongateway.shared.actors.PlayerActorFactory;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;

public final class PlayServer {
    private PlayServer() {
    }

    public static void configure(ZLinkFrameworkOptions options) {
        options.useRegistrySpotRemoteAddresses(SampleNames.SpotMesh);
        options.addSpotMesh(SampleNames.SpotMesh, mesh -> {
            mesh.addNode(SampleNames.PlayNode, node -> node.addSpotFactory(TicTacToeGameSpot.class));
            mesh.addNode(SampleNames.SessionRelayNode, node -> node.addSpotFactory(SessionRelaySpot.class));
        });
        options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory.class);
    }
}
