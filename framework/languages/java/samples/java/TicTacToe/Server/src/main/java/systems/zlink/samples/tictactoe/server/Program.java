package systems.zlink.samples.tictactoe.server;

import java.util.Arrays;
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
            .orElseThrow(() -> new IllegalArgumentException(usage()));
        SampleSettings settings = SampleSettings.fromArgs(args);
        switch (mode) {
            case "api" -> ApiServer.start(settings);
            case "play" -> PlayServer.start(settings);
            default -> throw new IllegalArgumentException(usage());
        }
    }

    private static String usage() {
        return "Usage: gradle :Server:run --args='[api|play] [--api-url URL] "
            + "[--api-bind URL] [--api-channel-endpoint tcp://HOST:PORT] "
            + "[--play-channel-endpoint tcp://HOST:PORT] "
            + "[--play-router-endpoint tcp://HOST:PORT] "
            + "[--play-endpoint tcp://HOST:PORT] "
            + "[--spot-endpoint tcp://HOST:PORT] [--log-dir DIR]'";
    }

    public static ServerHost startServer(
        SampleSettings settings,
        boolean startPlay,
        boolean startApi) {
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
            return new ServerHost(play, api);
        } catch (RuntimeException error) {
            close(api);
            close(play);
            throw error;
        }
    }

    public record ServerHost(
        ConfigurableApplicationContext play,
        ConfigurableApplicationContext api) implements AutoCloseable {
        @Override
        public void close() {
            Program.close(api);
            Program.close(play);
        }
    }

    private static void close(ConfigurableApplicationContext context) {
        if (context != null) {
            context.close();
        }
    }
}
