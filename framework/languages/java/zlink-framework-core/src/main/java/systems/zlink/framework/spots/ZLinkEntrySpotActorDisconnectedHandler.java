package systems.zlink.framework.spots;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;

public interface ZLinkEntrySpotActorDisconnectedHandler<
    TEntrySpot extends ZLinkEntrySpot,
    TActor extends ZLinkActor> {
    CompletionStage<Void> handleAsync(
        TEntrySpot entrySpot,
        TActor actor,
        CancellationToken cancellationToken);
}
