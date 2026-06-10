package systems.zlink.samples.tictactoe.server;

import java.util.Arrays;
import systems.zlink.samples.tictactoe.client.TicTacToeClient;
import systems.zlink.samples.tictactoe.client.TicTacToeClientOptions;
import systems.zlink.samples.tictactoe.server.api.ApiServerApplication;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.play.PlayServerApplication;

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
            case "all" -> runAll(settings);
            case "api" -> ApiServerApplication.run(settings);
            case "play" -> PlayServerApplication.run(settings);
            case "client" -> runClient(settings);
            default -> throw new IllegalArgumentException(usage());
        }
    }

    private static String usage() {
        return "Usage: gradle :Server:run --args='[all|play|api|client] [--api-url URL] "
            + "[--api-bind URL] [--api-channel-endpoint tcp://HOST:PORT] "
            + "[--play-channel-endpoint tcp://HOST:PORT] "
            + "[--play-router-endpoint tcp://HOST:PORT] "
            + "[--play-endpoint tcp://HOST:PORT] "
            + "[--spot-endpoint tcp://HOST:PORT] [--log-dir DIR]'";
    }

    private static void runAll(SampleSettings settings) throws Exception {
        SampleSettings effectiveSettings = settings.withEphemeralDefaults();
        try (AutoCloseable ignored = TicTacToeServerApplicationGroup.run(effectiveSettings)) {
            runClient(effectiveSettings);
        }
    }

    private static void runClient(SampleSettings settings) throws Exception {
        TicTacToeClientOptions defaults = TicTacToeClientOptions.createDefault();
        TicTacToeClientOptions options = new TicTacToeClientOptions(
            settings.apiPublicUrl(),
            defaults.gameName(),
            defaults.xActorId(),
            defaults.oActorId());
        new TicTacToeClient().run(options);
        System.out.println("tictactoe=completed");
    }
}
