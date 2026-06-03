package systems.zlink.samples.tictactoe.sessiongateway.server.api;

import java.util.concurrent.CountDownLatch;
import systems.zlink.framework.ZLinkFramework;
import systems.zlink.samples.tictactoe.sessiongateway.server.registry.RegistryServer;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        try (ZLinkFramework ignored = ApiServerHostFactory.start()) {
            new CountDownLatch(1).await();
        }
    }
}

final class ApiServerHostFactory {
    private ApiServerHostFactory() {
    }

    static ZLinkFramework start() {
        return ZLinkFramework.start(options -> {
            RegistryServer.configure(options);
            ApiServer.configure(options);
        });
    }
}
