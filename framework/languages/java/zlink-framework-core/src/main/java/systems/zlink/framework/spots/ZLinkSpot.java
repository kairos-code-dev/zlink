package systems.zlink.framework.spots;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.contracts.messaging.Message;

public interface ZLinkSpot<TActor extends ZLinkActor> {
    ZLinkSpotContext context();

    default void configure() {
    }

    default ZLinkSpotCreateResponse onCreate(Message request) {
        return ZLinkSpotCreateResponse.accept();
    }

    default void onInitialize() {
    }

    default void onClosing() {
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
