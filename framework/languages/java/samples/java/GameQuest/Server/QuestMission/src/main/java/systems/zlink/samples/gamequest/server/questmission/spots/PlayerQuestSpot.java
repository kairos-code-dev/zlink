package systems.zlink.samples.gamequest.server.questmission.spots;

import com.fasterxml.jackson.databind.ObjectMapper;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.messaging.ZLinkMessage;
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
    public ZLinkSpotCreateResponse onCreate(ZLinkMessage request) {
        PlayerQuestSpotCreateReq create = request.isEmpty()
            ? new PlayerQuestSpotCreateReq("")
            : request.decode(PlayerQuestSpotCreateReq.class);
        this.playerId = create.playerId();
        System.err.printf(
            "gamequest player quest spot ready player=%s spot=%s%n",
            playerId, context.spotRid());
        return ZLinkSpotCreateResponse.accept();
    }

    public record PlayerQuestSpotCreateReq(String playerId) {
    }
}
