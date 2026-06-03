package systems.zlink.samples.tictactoe.shared.contracts;

public record CreateGameHttpRes(
    String gameId,
    String playEndpoint,
    String gameName) {
}
