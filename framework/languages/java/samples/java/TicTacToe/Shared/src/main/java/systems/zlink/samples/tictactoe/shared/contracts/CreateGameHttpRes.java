package systems.zlink.samples.tictactoe.shared.contracts;

public record CreateGameHttpRes(
    String roomId,
    String playEndpoint,
    String gameName) {
}
