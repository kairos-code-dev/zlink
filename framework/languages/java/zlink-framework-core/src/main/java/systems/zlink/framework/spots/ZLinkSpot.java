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

    default void onPostActorJoined(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
    }

    default void onActorLeft(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
    }

    default void onActorDisconnected(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
    }
}
