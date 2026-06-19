package systems.zlink.samples.bingo.server.play.adapters.zlink.handlers;

public interface BingoMatchQueue {
    BingoMatchReservation reserve(
        String mode,
        String actorId,
        String preferredOwnerNodeRid,
        String newRoomId,
        int requiredPlayers);
}
