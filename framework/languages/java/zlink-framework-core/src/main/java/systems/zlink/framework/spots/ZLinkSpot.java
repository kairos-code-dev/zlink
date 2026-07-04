package systems.zlink.framework.spots;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.messaging.ZLinkMessage;

public interface ZLinkSpot<TActor extends ZLinkActor> extends ZLinkSpotActorLifecycle<TActor> {
    ZLinkSpotContext context();

    default void configure() {
    }

    default ZLinkSpotCreateResponse onCreate(ZLinkMessage request) {
        return ZLinkSpotCreateResponse.accept();
    }

    default void onInitialize() {
    }

    default void onClosing() {
    }

}
