package systems.zlink.framework.spots;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;

public interface ZLinkSpotActorJoinHandler<
    TSpot extends ZLinkSpot,
    TActor extends ZLinkActor,
    TRequest,
    TReply> {
    CompletionStage<TReply> handleAsync(
        TSpot spot,
        TActor actor,
        TRequest request,
        CancellationToken cancellationToken);
}
