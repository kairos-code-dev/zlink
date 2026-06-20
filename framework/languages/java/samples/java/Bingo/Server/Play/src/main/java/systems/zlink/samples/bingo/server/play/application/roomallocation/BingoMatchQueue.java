package systems.zlink.samples.bingo.server.play.application.roomallocation;

public interface BingoMatchQueue {
    BingoMatchReservation reserve(
        String mode,
        String actorId,
        String preferredOwnerNodeRid,
        String newRoomId,
        int requiredPlayers);
}
