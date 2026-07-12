package systems.zlink.framework.spots;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.messaging.ZLinkMessage;

public interface ZLinkSpotActorLifecycle<TActor extends ZLinkActor> {
    default CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
        String actorId,
        ZLinkMessage request) {
        return CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.reject());
    }

    CompletionStage<Void> onJoinedActor(TActor actor);

    CompletionStage<Void> onLeaveActor(TActor actor);

    default CompletionStage<Void> onDisconnectActor(TActor actor) {
        return CompletableFuture.completedFuture(null);
    }
}
