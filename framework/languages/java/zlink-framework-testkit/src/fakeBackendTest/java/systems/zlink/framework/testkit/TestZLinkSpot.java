package systems.zlink.framework.testkit;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.messaging.ZLinkMessage;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

public abstract class TestZLinkSpot<TActor extends ZLinkActor> implements ZLinkSpot<TActor> {
    @Override
    public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
        String actorId,
        ZLinkMessage request) {
        return CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.accept());
    }

    @Override
    public CompletionStage<Void> onJoinedActor(TActor actor) {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onLeaveActor(TActor actor) {
        return CompletableFuture.completedFuture(null);
    }
}
