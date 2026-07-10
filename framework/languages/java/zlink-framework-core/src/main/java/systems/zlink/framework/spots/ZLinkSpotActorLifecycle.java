package systems.zlink.framework.spots;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.messaging.ZLinkMessage;

public interface ZLinkSpotActorLifecycle<TActor extends ZLinkActor> {
    default ZLinkSpotActorJoinResponse onActorJoin(
        String actorId,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        return ZLinkSpotActorJoinResponse.reject();
    }

    void onJoinedActor(
        TActor actor,
        CancellationToken cancellationToken);

    void onLeaveActor(
        TActor actor,
        CancellationToken cancellationToken);

    default void onDisconnectActor(
        TActor actor,
        CancellationToken cancellationToken) {
    }
}
