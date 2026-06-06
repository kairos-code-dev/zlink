package systems.zlink.framework.spots;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;

public interface ZLinkSpotActorDisconnectedHandler<
    TSpot,
    TActor extends ZLinkActor> {
    CompletionStage<Void> handleAsync(
        TSpot spot,
        TActor actor,
        CancellationToken cancellationToken);
}
