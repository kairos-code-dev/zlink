package systems.zlink.samples.tictactoe.shared.contracts;

public record PlayerJoinedNotify(String roomId, String actorId, String mark, GameState state) {
}
