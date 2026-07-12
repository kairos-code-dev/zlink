package systems.zlink.framework.actors;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.messaging.ZLinkMessage;

/**
 * Converts actor domain state when an actor moves between Spot nodes.
 * Admission, routing, membership, and location ownership remain framework
 * responsibilities.
 */
public interface ZLinkActorTransferAdapter<TActor extends ZLinkActor> {
    CompletionStage<ZLinkMessage> transferOut(TActor actor);

    CompletionStage<TActor> transferIn(
        String actorId,
        ZLinkActorContext context,
        ZLinkMessage state);
}
