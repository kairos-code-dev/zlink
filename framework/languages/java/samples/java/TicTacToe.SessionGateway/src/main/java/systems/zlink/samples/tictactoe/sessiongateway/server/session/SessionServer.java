package systems.zlink.samples.tictactoe.sessiongateway.server.session;

import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.PlayerSession;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;

public final class SessionServer {
    private SessionServer() {
    }

    public static void configure(ZLinkFrameworkOptions options) {
        options.addStreamNode(SampleNames.GatewayStream, stream -> {
            stream.bind(SampleTopology.SessionEndpoint);
            stream.attachActorGateway(SampleNames.SessionRelayNode);
            stream.registerSession(PlayerSession.class);
        });
    }
}
