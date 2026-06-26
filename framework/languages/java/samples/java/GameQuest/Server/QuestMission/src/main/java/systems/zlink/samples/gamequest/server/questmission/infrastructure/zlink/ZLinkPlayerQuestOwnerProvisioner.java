package systems.zlink.samples.gamequest.server.questmission.infrastructure.zlink;

import static systems.zlink.framework.ZLinkAwait.await;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.nio.charset.StandardCharsets;
import org.springframework.stereotype.Component;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.gamequest.server.questmission.application.QuestEventProcessor;
import systems.zlink.samples.gamequest.server.questmission.spots.playerquestspot.PlayerQuestSpot;

@Component
public final class ZLinkPlayerQuestOwnerProvisioner implements QuestEventProcessor.PlayerQuestOwnerProvisioner {
    private final ZLinkSpotManager spots;
    private final ObjectMapper json;

    public ZLinkPlayerQuestOwnerProvisioner(ZLinkSpotManager spots, ObjectMapper json) {
        this.spots = spots;
        this.json = json;
    }

    @Override
    public void ensure(String playerId) {
        await(spots.getOrCreate(
            PlayerQuestSpot.class,
            RoutingId.from(("player:" + playerId).getBytes(StandardCharsets.UTF_8)),
            new PlayerQuestSpot.PlayerQuestSpotCreateReq(playerId)));
    }
}
