package systems.zlink.samples.tictactoe.server;

import java.util.Arrays;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.springframework.context.ConfigurableApplicationContext;
import systems.zlink.samples.tictactoe.client.TicTacToeClient;
import systems.zlink.samples.tictactoe.client.TicTacToeClientOptions;
import systems.zlink.samples.tictactoe.client.TicTacToeClientResult;
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
            case "client" -> runClient(settings);
            default -> throw new IllegalArgumentException(
                "Usage: gradle :Server:run --args='[all|server|api|play|client] [--api-url URL] [--api-bind URL] [--api-channel-endpoint tcp://HOST:PORT] [--play-channel-endpoint tcp://HOST:PORT] [--play-router-endpoint tcp://HOST:PORT] [--play-endpoint tcp://HOST:PORT] [--spot-endpoint tcp://HOST:PORT] [--log-dir DIR]'");
        }
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

    private static void runServer(
        SampleSettings settings,
        boolean startPlay,
        boolean startApi) {
        startServer(settings, startPlay, startApi);
    }

    private static void runClient(SampleSettings settings) throws Exception {
        TicTacToeClientResult result = awaitSample(new TicTacToeClient().run(
            new TicTacToeClientOptions(
                settings.apiPublicUrl(),
                "tictactoe-game",
                "player-x",
                "player-o")));
        result.writeTo(System.out);
    }

    private static <T> T awaitSample(CompletionStage<T> stage) throws Exception {
        CompletableFuture<T> done = new CompletableFuture<>();
        stage.whenComplete((value, error) -> {
            if (error != null) {
                done.completeExceptionally(error);
            } else {
                done.complete(value);
            }
        });
        return done.get();
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
