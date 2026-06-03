package systems.zlink.samples.tictactoe.client;

public record TicTacToeClientOptions(
    String gameName,
    String hostAccessToken,
    String guestAccessToken,
    String apiEndpoint,
    String playEndpoint) {
    public static TicTacToeClientOptions createDefault() {
        return new TicTacToeClientOptions(
            "Morning game",
            TicTacToeSampleDefaults.HostAccessToken,
            TicTacToeSampleDefaults.GuestAccessToken,
            TicTacToeSampleDefaults.ApiEndpoint,
            TicTacToeSampleDefaults.PlayEndpoint);
    }
}
