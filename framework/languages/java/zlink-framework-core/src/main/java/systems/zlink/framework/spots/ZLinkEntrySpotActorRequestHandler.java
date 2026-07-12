package systems.zlink.framework.spots;

import systems.zlink.framework.actors.ZLinkActor;

public interface ZLinkEntrySpotActorRequestHandler<
    TEntrySpot extends ZLinkEntrySpot<?>,
    TActor extends ZLinkActor,
    TRequest,
    TReply> {
    TReply handle(
        TEntrySpot entrySpot,
        TActor actor,
        ZLinkSpotActorRequestContext context,
        TRequest request);
}
