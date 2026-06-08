package systems.zlink.samples.bingo.server.play.bingoroomspots.handlers;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomSpot;
import systems.zlink.samples.bingo.server.play.domain.bingo.BingoRoomModels;

public final class BingoRoomSpotCreatedHandler {
    private final ObjectMapper json;

    public BingoRoomSpotCreatedHandler(ObjectMapper json) {
        this.json = json;
    }

    public CompletionStage<ZLinkSpotCreateResponse> handleAsync(
        BingoRoomSpot spot,
        Message request) {
        spot.applySettings(decodeSettings(request));
        return CompletableFuture.completedFuture(ZLinkSpotCreateResponse.accept());
    }

    private BingoRoomModels.BingoRoomSettings decodeSettings(Message request) {
        if (request.isEmpty()) {
            return BingoRoomModels.BingoRoomSettings.create("standard", 0);
        }
        try {
            return json.readValue(
                request.toByteArray(),
                BingoRoomModels.BingoRoomSettings.class);
        } catch (IOException ex) {
            throw new IllegalStateException("Failed to decode bingo room settings.", ex);
        }
    }
}
