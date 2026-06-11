package systems.zlink.samples.bingo.server.play.adapters.zlink.handlers;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.concurrent.CompletionStage;
import java.util.HashMap;
import java.util.Map;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.bingo.server.play.adapters.zlink.spots.BingoRoomSpot;
import systems.zlink.samples.bingo.server.play.domain.bingo.BingoRoomModels;

public final class BingoRoomDirectory {
    private final ZLinkSpotManager spots;
    private final ObjectMapper json;
    private final Object gate = new Object();
    private final Map<String, RoomAllocation> actorAllocations = new HashMap<>();
    private String currentRoomId;
    private BingoRoomModels.BingoRoomSettings currentRoomSettings;
    private int reservedSeats;
    private int roomSeq;

    public BingoRoomDirectory(
        ZLinkSpotManager spots,
        ObjectMapper json) {
        this.spots = spots;
        this.json = json;
    }

    public CompletionStage<String> allocateAsync(String actorId, String mode) {
        if (actorId == null || actorId.isBlank()) {
            return java.util.concurrent.CompletableFuture.failedFuture(
                new IllegalStateException("actorId is required"));
        }
        if (!"two-player".equals(mode)) {
            return java.util.concurrent.CompletableFuture.failedFuture(
                new IllegalStateException("Unsupported bingo mode. mode=" + mode));
        }

        String roomId;
        BingoRoomModels.BingoRoomSettings settings;
        synchronized (gate) {
            RoomAllocation existing = actorAllocations.get(actorId);
            if (existing != null) {
                roomId = existing.roomId();
                settings = existing.settings();
            } else {
                settings = BingoRoomModels.BingoRoomSettings.create(mode, roomSeq + 1);
                if (currentRoomId == null
                    || currentRoomSettings == null
                    || !currentRoomSettings.mode().equals(settings.mode())
                    || reservedSeats >= currentRoomSettings.requiredPlayers()) {
                    settings = BingoRoomModels.BingoRoomSettings.create(mode, ++roomSeq);
                    currentRoomId = "bingo-room-%03d".formatted(roomSeq);
                    currentRoomSettings = settings;
                    reservedSeats = 0;
                }
                reservedSeats++;
                roomId = currentRoomId;
                actorAllocations.put(actorId, new RoomAllocation(roomId, settings));
            }
        }

        Message settingsPart = serialize(settings);
        return spots.getOrCreate(BingoRoomSpot.class, RoutingId.from(roomId), settingsPart)
            .thenApply(ignored -> roomId)
            .whenComplete((ignored, error) -> settingsPart.close());
    }

    private Message serialize(BingoRoomModels.BingoRoomSettings settings) {
        try {
            return Message.from(json.writeValueAsBytes(settings));
        } catch (JsonProcessingException ex) {
            throw new IllegalStateException("Failed to encode bingo room settings.", ex);
        }
    }

    private record RoomAllocation(
        String roomId,
        BingoRoomModels.BingoRoomSettings settings) {
    }
}
