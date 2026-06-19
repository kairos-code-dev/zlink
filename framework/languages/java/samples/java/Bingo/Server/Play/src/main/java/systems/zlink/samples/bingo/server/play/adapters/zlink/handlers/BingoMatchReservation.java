package systems.zlink.samples.bingo.server.play.adapters.zlink.handlers;

public record BingoMatchReservation(
    String roomId,
    String ownerPlayNodeRid) {
}
