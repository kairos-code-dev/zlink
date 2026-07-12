package systems.zlink.framework.spots;

import systems.zlink.framework.actors.ZLinkActor;

public interface ZLinkSpotActorSendHandler<
    TSpot extends ZLinkSpot<?>,
    TActor extends ZLinkActor,
    TMessage> {
    void handle(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorSendContext context,
        TMessage message);
}
