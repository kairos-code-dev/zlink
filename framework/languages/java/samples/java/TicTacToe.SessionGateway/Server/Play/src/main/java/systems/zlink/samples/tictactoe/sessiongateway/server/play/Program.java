package systems.zlink.samples.tictactoe.sessiongateway.server.play;

import java.util.concurrent.CountDownLatch;
import systems.zlink.framework.ZLinkFramework;
import systems.zlink.samples.tictactoe.sessiongateway.server.registry.RegistryServer;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        try (ZLinkFramework ignored = PlayServerHostFactory.start()) {
            new CountDownLatch(1).await();
        }
    }
}

final class PlayServerHostFactory {
    private PlayServerHostFactory() {
    }

    static ZLinkFramework start() {
        return ZLinkFramework.start(options -> {
            RegistryServer.configure(options);
            PlayServer.configure(options);
        });
    }
}
