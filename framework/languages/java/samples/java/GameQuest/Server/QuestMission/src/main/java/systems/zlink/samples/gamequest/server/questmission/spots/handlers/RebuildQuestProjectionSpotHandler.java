package systems.zlink.samples.gamequest.server.questmission.spots.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.samples.gamequest.server.questmission.spots.PlayerQuestSpot;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

public final class RebuildQuestProjectionSpotHandler
    implements ZLinkSpotRequestHandler<PlayerQuestSpot, Messages.RebuildQuestProjectionReq, Messages.QuestProgress> {
    @Override
    public CompletionStage<Messages.QuestProgress> handle(
        PlayerQuestSpot spot,
        Messages.RebuildQuestProjectionReq request) {
        spot.requirePlayer(request.playerId());
        return CompletableFuture.completedFuture(
            spot.store().rebuildProjection(spot.playerId(), request.questId(), request.count()));
    }
}
