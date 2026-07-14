package systems.zlink.samples.gamequest.server.questmission.spots;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.samples.gamequest.server.questmission.store.QuestStore;

public final class PlayerQuestSpot implements ZLinkSpot<ZLinkActor> {
    private final ZLinkSpotContext context;
    private final QuestStore store;
    private String playerId;

    public PlayerQuestSpot(ZLinkSpotContext context, QuestStore store) {
        this.context = context;
        this.store = store;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResponse> onCreate(ZLinkMessage request) {
        PlayerQuestCreateReq create = request.decode(PlayerQuestCreateReq.class);
        if (!RoutingId.from(create.playerId()).equals(context.spotRid())) {
            return CompletableFuture.completedFuture(ZLinkSpotCreateResponse.reject());
        }
        playerId = create.playerId();
        store.activate(playerId);
        return CompletableFuture.completedFuture(ZLinkSpotCreateResponse.accept());
    }

    public String playerId() {
        return playerId;
    }

    public void requirePlayer(String requestedPlayerId) {
        if (!playerId.equals(requestedPlayerId)) {
            throw new IllegalArgumentException(
                "request player does not match owner Spot: " + requestedPlayerId);
        }
    }

    public QuestStore store() {
        return store;
    }

    @Override
    public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(String actorId, ZLinkMessage request) {
        return CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.reject());
    }

    @Override
    public CompletionStage<Void> onJoinedActor(ZLinkActor actor) {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onLeaveActor(ZLinkActor actor) {
        return CompletableFuture.completedFuture(null);
    }
}
