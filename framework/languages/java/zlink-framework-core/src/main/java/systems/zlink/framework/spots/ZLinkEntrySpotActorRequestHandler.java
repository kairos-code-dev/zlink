package systems.zlink.framework.spots;

import systems.zlink.framework.actors.ZLinkActor;
import java.util.concurrent.CompletionStage;

public interface ZLinkEntrySpotActorRequestHandler<
    TEntrySpot extends ZLinkEntrySpot<?>,
    TActor extends ZLinkActor,
    TRequest,
    TReply> {
    CompletionStage<TReply> handle(
        TEntrySpot entrySpot,
        TActor actor,
        ZLinkSpotActorRequestContext context,
        TRequest request);
}
