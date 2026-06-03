package systems.zlink.samples.tictactoe.sessiongateway.server.session;

import java.util.concurrent.CountDownLatch;
import systems.zlink.framework.ZLinkFramework;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.PlayServer;
import systems.zlink.samples.tictactoe.sessiongateway.server.registry.RegistryServer;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        try (ZLinkFramework ignored = SessionServerHostFactory.start()) {
            new CountDownLatch(1).await();
        }
    }
}

final class SessionServerHostFactory {
    private SessionServerHostFactory() {
    }

    static ZLinkFramework start() {
        return ZLinkFramework.start(options -> {
            RegistryServer.configure(options);
            PlayServer.configureSessionRelayNode(options);
            SessionServer.configure(options);
        });
    }
}
