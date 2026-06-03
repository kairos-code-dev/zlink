package systems.zlink.samples.tictactoe.server;

import java.util.Arrays;
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
            case "all", "server" -> runServer(options -> {
                ApiServer.configure(options);
                PlayServer.configure(options);
            });
            case "api" -> runServer(ApiServer::configure);
            case "play" -> runServer(PlayServer::configure);
            default -> throw new IllegalArgumentException(
                "Usage: gradle :Server:run --args='[all|server|api|play]'");
        }
    }

    private static void runServer(Consumer<ZLinkFrameworkOptions> configure) throws Exception {
        try (ZLinkFramework ignored = ZLinkFramework.start(configure)) {
            new CountDownLatch(1).await();
        }
    }

}
