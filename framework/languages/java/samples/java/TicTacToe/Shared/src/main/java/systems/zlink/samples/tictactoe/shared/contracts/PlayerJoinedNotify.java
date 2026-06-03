package systems.zlink.samples.tictactoe.shared.contracts;

public record PlayerJoinedNotify(String gameId, String actorId, String mark, GameState state) {
}
