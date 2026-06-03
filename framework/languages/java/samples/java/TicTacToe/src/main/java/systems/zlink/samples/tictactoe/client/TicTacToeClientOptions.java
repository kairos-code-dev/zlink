package systems.zlink.samples.tictactoe.client;

public record TicTacToeClientOptions(
    String gameName,
    String hostAccessToken,
    String guestAccessToken,
    String apiEndpoint,
    String playEndpoint) {
}
