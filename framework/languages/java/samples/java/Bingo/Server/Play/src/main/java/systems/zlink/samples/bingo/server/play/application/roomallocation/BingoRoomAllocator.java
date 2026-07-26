package systems.zlink.samples.bingo.server.play.application.roomallocation;

import systems.zlink.samples.bingo.server.play.domain.bingo.BingoRoomModels;

public final class BingoRoomAllocator {
    private final BingoMatchQueue matchQueue;
    private final long drawPeriodMillis;
    private int roomSeq;

    public BingoRoomAllocator(BingoMatchQueue matchQueue, long drawPeriodMillis) {
        this.matchQueue = matchQueue;
        this.drawPeriodMillis = drawPeriodMillis;
    }

    public BingoRoomAllocation allocate(String actorId, String mode) {
        if (actorId == null || actorId.isBlank()) {
            throw new IllegalStateException("actorId is required");
        }
        if (!"two-player".equals(mode)) {
            throw new IllegalStateException("Unsupported bingo mode. mode=" + mode);
        }

        int roomSeq = nextRoomSeq();
        BingoRoomModels.BingoRoomSettings settings =
            BingoRoomModels.BingoRoomSettings.create(mode, roomSeq, drawPeriodMillis);
        BingoMatchReservation reservation = matchQueue.reserve(
            mode,
            actorId,
            settings.mode() + "-room-" + roomSeq,
            settings.requiredPlayers());
        return new BingoRoomAllocation(reservation.roomId(), settings);
    }

    private synchronized int nextRoomSeq() {
        roomSeq += 1;
        return roomSeq;
    }

}
