package systems.zlink.samples.tictactoe.server;

import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import org.springframework.context.ConfigurableApplicationContext;
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
            case "all", "server" -> runServer(settings, true, true);
            case "api" -> runServer(settings, false, true);
            case "play" -> runServer(settings, true, false);
            default -> throw new IllegalArgumentException(
                "Usage: gradle :Server:run --args='[all|server|api|play] [--api-url URL] [--api-bind URL] [--api-channel-endpoint tcp://HOST:PORT] [--play-channel-endpoint tcp://HOST:PORT] [--play-endpoint tcp://HOST:PORT] [--spot-endpoint tcp://HOST:PORT] [--log-dir DIR]'");
        }
    }

    private static void runServer(
        SampleSettings settings,
        boolean startPlay,
        boolean startApi) throws Exception {
        SampleSettings.setCurrent(settings);
        ConfigurableApplicationContext play = null;
        ConfigurableApplicationContext api = null;
        try {
            if (startPlay) {
                play = PlayServer.start(settings);
            }
            if (startApi) {
                api = ApiServer.start(settings);
            }
            new CountDownLatch(1).await();
        } finally {
            if (api != null) {
                api.close();
            }
            if (play != null) {
                play.close();
            }
        }
    }
}
