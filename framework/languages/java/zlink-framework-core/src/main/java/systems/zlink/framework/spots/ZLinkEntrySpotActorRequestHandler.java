package systems.zlink.framework.spots;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;

public interface ZLinkEntrySpotActorRequestHandler<
    TEntrySpot extends ZLinkEntrySpot,
    TActor extends ZLinkActor,
    TRequest,
    TReply> {
    CompletionStage<TReply> handleAsync(
        TEntrySpot entrySpot,
        TActor actor,
        ZLinkSpotActorRequestContext context,
        TRequest request,
        CancellationToken cancellationToken);
}
