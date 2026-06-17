package systems.zlink.framework.spots;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.contracts.messaging.Message;

public interface ZLinkEntrySpot<TActor extends ZLinkActor> {
    ZLinkEntrySpotContext context();

    default void configure() {
    }

    default void onInitialize() {
    }

    default void onClosing() {
    }

    default void onCreateActor(
        TActor actor,
        CancellationToken cancellationToken) {
    }

    default ZLinkSpotActorJoinResponse onActorJoin(
        TActor actor,
        Message request,
        CancellationToken cancellationToken) {
        return ZLinkSpotActorJoinResponse.reject();
    }

    default void onJoinedActor(
        TActor actor,
        CancellationToken cancellationToken) {
    }

    default void onLeaveActor(
        TActor actor,
        CancellationToken cancellationToken) {
    }

    default void onDisconnectActor(
        TActor actor,
        CancellationToken cancellationToken) {
    }
}
