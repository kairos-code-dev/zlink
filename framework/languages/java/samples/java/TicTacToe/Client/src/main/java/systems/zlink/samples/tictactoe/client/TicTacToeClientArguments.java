package systems.zlink.samples.tictactoe.client;

public final class TicTacToeClientArguments {
    private TicTacToeClientArguments() {
    }

    public static TicTacToeClientOptions parse(String[] args) {
        TicTacToeClientOptions defaults = TicTacToeClientOptions.createDefault();
        return new TicTacToeClientOptions(
            readOption(args, "--game-name", defaults.gameName()),
            readOption(args, "--host-access-token", defaults.hostAccessToken()),
            readOption(args, "--guest-access-token", defaults.guestAccessToken()),
            readOption(args, "--api-endpoint", defaults.apiEndpoint()),
            readOption(args, "--play-endpoint", defaults.playEndpoint()));
    }

    private static String readOption(
        String[] args,
        String name,
        String defaultValue) {
        for (int index = 0; index < args.length; index++) {
            if (!args[index].equals(name)) {
                continue;
            }
            if (index + 1 >= args.length) {
                throw new IllegalArgumentException("Missing value for '" + name + "'.");
            }
            return args[index + 1];
        }
        return defaultValue;
    }
}
