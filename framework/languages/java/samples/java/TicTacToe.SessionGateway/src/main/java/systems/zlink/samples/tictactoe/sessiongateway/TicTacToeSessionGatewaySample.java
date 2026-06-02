package systems.zlink.samples.tictactoe.sessiongateway;

import systems.zlink.framework.ZLinkFramework;
import systems.zlink.framework.ZLinkRegistry;
import systems.zlink.samples.tictactoe.sessiongateway.client.SessionActorDispatchClient;
import systems.zlink.samples.tictactoe.sessiongateway.client.SessionActorDispatchClientOptions;
import systems.zlink.samples.tictactoe.sessiongateway.server.api.ApiServer;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.PlayServer;
import systems.zlink.samples.tictactoe.sessiongateway.server.registry.RegistryServer;
import systems.zlink.samples.tictactoe.sessiongateway.server.session.SessionServer;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;

public final class TicTacToeSessionGatewaySample {
    private TicTacToeSessionGatewaySample() {
    }

    public static void main(String[] args) throws Exception {
        SessionActorDispatchClient client = new SessionActorDispatchClient();
        try (ZLinkRegistry registry = ZLinkRegistry.start(options -> {
                 options.setPubEndpoint(SampleTopology.RegistryPubEndpoint);
                 options.setRouterEndpoint(SampleTopology.RegistryRouterEndpoint);
             });
             ZLinkFramework framework = ZLinkFramework.start(options -> {
            RegistryServer.configure(options);
            ApiServer.configure(options);
            PlayServer.configure(options);
            SessionServer.configure(options);
        })) {
            client.verifyFrameworkActorCreation(framework.actorManager());
            client.runReconnectScenario(
                framework,
                new SessionActorDispatchClientOptions("player-1"));
        }

        System.out.println("TicTacToe.SessionGateway sample self-check passed");
    }
}
