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

    default void onCreateActor(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
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
