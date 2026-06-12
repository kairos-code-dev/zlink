package systems.zlink.framework.spots;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;

public interface ZLinkEntrySpot {
    ZLinkEntrySpotContext context();

    default void configure() {
    }

    default void onInitialize() {
    }

    default void onClosing() {
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
