package systems.zlink.samples.tictactoe.server;

import java.util.Arrays;
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
            .orElseThrow(() -> new IllegalArgumentException(usage()));
        SampleSettings settings = SampleSettings.load(args);
        switch (mode) {
            case "api" -> ApiServerApplication.run(settings);
            case "play" -> PlayServerApplication.run(settings);
            default -> throw new IllegalArgumentException(usage());
        }
    }

    private static String usage() {
        return "Usage: gradle :Server:run --args='[play|api] [--config PATH] [--api-url URL] "
            + "[--api-bind URL] [--api-channel-endpoint tcp://HOST:PORT] "
            + "[--play-channel-endpoint tcp://HOST:PORT] "
            + "[--play-endpoint tcp://HOST:PORT] "
            + "[--spot-endpoint tcp://HOST:PORT] [--log-dir DIR]'";
    }
}
