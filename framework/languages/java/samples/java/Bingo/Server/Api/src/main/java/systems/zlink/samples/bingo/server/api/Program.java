package systems.zlink.samples.bingo.server.api;

import java.util.concurrent.CountDownLatch;
import systems.zlink.framework.ZLinkFramework;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        try (ZLinkFramework ignored = ApiServerHostFactory.start()) {
            new CountDownLatch(1).await();
        }
    }
}
