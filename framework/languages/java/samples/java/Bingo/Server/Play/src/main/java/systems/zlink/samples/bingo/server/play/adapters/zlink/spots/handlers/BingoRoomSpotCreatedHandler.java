package systems.zlink.samples.bingo.server.play.adapters.zlink.spots.handlers;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.samples.bingo.server.configuration.SampleTimings;
import systems.zlink.samples.bingo.server.play.adapters.zlink.spots.BingoRoomSpot;
import systems.zlink.samples.bingo.server.play.domain.bingo.BingoRoomModels;

public final class BingoRoomSpotCreatedHandler {
    private final ObjectMapper json;

    public BingoRoomSpotCreatedHandler(ObjectMapper json) {
        this.json = json;
    }

    public ZLinkSpotCreateResponse handle(
        BingoRoomSpot spot,
        Message request) {
        spot.applySettings(decodeSettings(request));
        return ZLinkSpotCreateResponse.accept();
    }

    private BingoRoomModels.BingoRoomSettings decodeSettings(Message request) {
        if (request.isEmpty()) {
            return BingoRoomModels.BingoRoomSettings.create(
                "two-player",
                0,
                SampleTimings.DrawPeriod.toMillis());
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
