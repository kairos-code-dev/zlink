package systems.zlink.samples.bingo.server.registry;

import java.util.concurrent.CountDownLatch;
import systems.zlink.framework.ZLinkRegistry;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        try (ZLinkRegistry ignored = RegistryHostFactory.start()) {
            new CountDownLatch(1).await();
        }
    }
}
