package systems.zlink.samples.tictactoe.server;

import java.util.Arrays;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.function.Consumer;
import systems.zlink.framework.ZLinkFramework;
import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.samples.tictactoe.server.api.ApiHttpServer;
import systems.zlink.samples.tictactoe.server.api.ApiServer;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.play.PlayServer;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        String mode = Arrays.stream(args)
            .filter(arg -> !arg.startsWith("--"))
            .findFirst()
            .orElse("all");
        SampleSettings settings = SampleSettings.fromArgs(args);
        switch (mode) {
            case "all", "server" -> runServer(settings, true, PlayServer::configure, ApiServer::configure);
            case "api" -> runServer(settings, true, ApiServer::configure);
            case "play" -> runServer(settings, false, PlayServer::configure);
            default -> throw new IllegalArgumentException(
                "Usage: gradle :Server:run --args='[all|server|api|play] [--api-url URL] [--api-bind URL] [--api-channel-endpoint tcp://HOST:PORT] [--play-channel-endpoint tcp://HOST:PORT] [--play-endpoint tcp://HOST:PORT] [--spot-endpoint tcp://HOST:PORT] [--log-dir DIR]'");
        }
    }

    @SafeVarargs
    private static void runServer(
        Consumer<ZLinkFrameworkOptions>... configureHosts) throws Exception {
        runServer(SampleSettings.current(), false, configureHosts);
    }

    @SafeVarargs
    private static void runServer(
        SampleSettings settings,
        boolean startHttpApi,
        Consumer<ZLinkFrameworkOptions>... configureHosts) throws Exception {
        SampleSettings.setCurrent(settings);
        List<ZLinkFramework> hosts = new java.util.ArrayList<>();
        ApiHttpServer httpApi = null;
        try {
            for (Consumer<ZLinkFrameworkOptions> configure : configureHosts) {
                hosts.add(ZLinkFramework.start(configure));
            }
            if (startHttpApi) {
                httpApi = ApiHttpServer.start(hosts.get(hosts.size() - 1).client(), settings);
            }
            new CountDownLatch(1).await();
        } finally {
            if (httpApi != null) {
                httpApi.close();
            }
            for (int i = hosts.size() - 1; i >= 0; i--) {
                hosts.get(i).close();
            }
        }
    }
}
