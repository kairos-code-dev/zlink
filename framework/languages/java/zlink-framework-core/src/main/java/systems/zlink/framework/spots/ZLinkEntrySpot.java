package systems.zlink.framework.spots;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.CancellationToken;

public interface ZLinkEntrySpot<TActor extends ZLinkActor> extends ZLinkSpotActorLifecycle<TActor> {
    ZLinkEntrySpotContext context();

    default void configure() {
    }

    default void onInitialize() {
    }

    default void onClosing() {
    }

    default void onCreateActor(
        TActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken) {
    }

}
