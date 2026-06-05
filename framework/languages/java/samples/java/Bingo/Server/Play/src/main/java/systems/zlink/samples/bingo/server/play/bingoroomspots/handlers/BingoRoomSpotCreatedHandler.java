package systems.zlink.samples.bingo.server.play.bingoroomspots.handlers;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomModels;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomSpot;

public final class BingoRoomSpotCreatedHandler {
    private final ObjectMapper json;

    public BingoRoomSpotCreatedHandler(ObjectMapper json) {
        this.json = json;
    }

    public CompletionStage<Void> handleAsync(
        BingoRoomSpot spot,
        List<Message> createParts) {
        spot.applySettings(decodeSettings(createParts));
        return CompletableFuture.completedFuture(null);
    }

    private BingoRoomModels.BingoRoomSettings decodeSettings(List<Message> createParts) {
        if (createParts.isEmpty()) {
            return BingoRoomModels.BingoRoomSettings.create("four-player", 0);
        }
        if (createParts.size() != 1) {
            throw new IllegalStateException(
                "Bingo room expects exactly one create payload part, but received "
                    + createParts.size() + ".");
        }
        try {
            return json.readValue(
                createParts.getFirst().toByteArray(),
                BingoRoomModels.BingoRoomSettings.class);
        } catch (IOException ex) {
            throw new IllegalStateException("Failed to decode bingo room settings.", ex);
        }
    }
}
