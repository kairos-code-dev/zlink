package systems.zlink.framework.spots;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;

public interface ZLinkEntrySpotActorSendHandler<
    TEntrySpot extends ZLinkEntrySpot,
    TActor extends ZLinkActor,
    TMessage> {
    CompletionStage<Void> handleAsync(
        TEntrySpot entrySpot,
        TActor actor,
        ZLinkSpotActorSendContext context,
        TMessage message,
        CancellationToken cancellationToken);
}
