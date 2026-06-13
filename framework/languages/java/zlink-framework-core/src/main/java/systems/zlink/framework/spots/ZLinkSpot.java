package systems.zlink.framework.spots;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.contracts.messaging.Message;

public interface ZLinkSpot {
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
        ZLinkActor actor,
        Message request,
        CancellationToken cancellationToken) {
        return ZLinkSpotActorJoinResponse.reject();
    }

    default void onJoinActor(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
    }

    default void onLeaveActor(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
    }

    default void onDisconnectActor(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
    }
}
