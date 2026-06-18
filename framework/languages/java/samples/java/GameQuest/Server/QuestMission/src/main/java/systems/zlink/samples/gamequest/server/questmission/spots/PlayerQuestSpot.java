package systems.zlink.samples.gamequest.server.questmission.spots;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;

/**
 * Owner aggregate per player, routed by {@code playerId}. Mirrors the .NET
 * {@code PlayerQuestSpot}: the QuestMission instance that owns the player hosts
 * exactly one spot per player.
 */
public final class PlayerQuestSpot implements ZLinkSpot<ZLinkActor> {
    private final ZLinkSpotContext context;
    private final ObjectMapper json;
    private String playerId = "";

    public PlayerQuestSpot(ZLinkSpotContext context, ObjectMapper json) {
        this.context = context;
        this.json = json;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    public String playerId() {
        return playerId;
    }

    @Override
    public ZLinkSpotCreateResponse onCreate(Message request) {
        this.playerId = decode(request).playerId();
        System.err.printf(
            "gamequest player quest spot ready player=%s spot=%s%n",
            playerId, context.spotRid().toHex());
        return ZLinkSpotCreateResponse.accept();
    }

    private PlayerQuestSpotCreateReq decode(Message request) {
        byte[] bytes = request.toByteArray();
        if (bytes.length == 0) {
            return new PlayerQuestSpotCreateReq("");
        }
        try {
            return json.readValue(bytes, PlayerQuestSpotCreateReq.class);
        } catch (IOException ex) {
            throw new IllegalArgumentException("Failed to decode PlayerQuestSpotCreateReq.", ex);
        }
    }

    public record PlayerQuestSpotCreateReq(String playerId) {
    }
}
