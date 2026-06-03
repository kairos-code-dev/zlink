package systems.zlink.samples.tictactoe.server;

import java.util.Arrays;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.function.Consumer;
import systems.zlink.framework.ZLinkFramework;
import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.samples.tictactoe.server.api.ApiServer;
import systems.zlink.samples.tictactoe.server.play.PlayServer;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        String mode = Arrays.stream(args)
            .filter(arg -> !arg.startsWith("--"))
            .findFirst()
            .orElse("server");
        switch (mode) {
            case "all", "server" -> runServer(PlayServer::configure, ApiServer::configure);
            case "api" -> runServer(ApiServer::configure);
            case "play" -> runServer(PlayServer::configure);
            default -> throw new IllegalArgumentException(
                "Usage: gradle :Server:run --args='[all|server|api|play]'");
        }
    }

    @SafeVarargs
    private static void runServer(
        Consumer<ZLinkFrameworkOptions>... configureHosts) throws Exception {
        List<ZLinkFramework> hosts = new java.util.ArrayList<>();
        try {
            for (Consumer<ZLinkFrameworkOptions> configure : configureHosts) {
                hosts.add(ZLinkFramework.start(configure));
            }
            new CountDownLatch(1).await();
        } finally {
            for (int i = hosts.size() - 1; i >= 0; i--) {
                hosts.get(i).close();
            }
        }
    }
}
