package systems.zlink.samples.bingo.server.play.handlers;

import java.util.concurrent.CompletionStage;
import java.util.HashMap;
import java.util.Map;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomSpot;

public final class BingoRoomDirectory {
    private final ZLinkSpotManager spots;
    private final Object gate = new Object();
    private final Map<String, String> actorRooms = new HashMap<>();
    private String currentRoomId;
    private int reservedSeats;
    private int roomSeq;

    public BingoRoomDirectory(ZLinkSpotManager spots) {
        this.spots = spots;
    }

    public CompletionStage<String> allocateAsync(String actorId, String mode) {
        if (actorId == null || actorId.isBlank()) {
            return java.util.concurrent.CompletableFuture.failedFuture(
                new IllegalStateException("actorId is required"));
        }
        if (!"four-player".equals(mode)) {
            return java.util.concurrent.CompletableFuture.failedFuture(
                new IllegalStateException("Unsupported bingo mode. mode=" + mode));
        }

        String roomId;
        synchronized (gate) {
            String existing = actorRooms.get(actorId);
            if (existing != null) {
                roomId = existing;
            } else {
                if (currentRoomId == null || reservedSeats >= 4) {
                    roomSeq++;
                    currentRoomId = RoutingId.from("bingo-room-%03d".formatted(roomSeq)).toHex();
                    reservedSeats = 0;
                }
                reservedSeats++;
                roomId = currentRoomId;
                actorRooms.put(actorId, roomId);
            }
        }

        return spots.getOrCreateAsync(BingoRoomSpot.class, RoutingId.fromHex(roomId))
            .thenApply(ignored -> roomId);
    }
}
