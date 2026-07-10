package systems.zlink.framework.actors;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.messaging.ZLinkMessage;

/**
 * Converts actor domain state when an actor moves between Spot nodes.
 * Admission, routing, membership, and location ownership remain framework
 * responsibilities.
 */
public interface ZLinkActorTransferAdapter<TActor extends ZLinkActor> {
    ZLinkMessage transferOut(
        TActor actor,
        CancellationToken cancellationToken);

    TActor transferIn(
        String actorId,
        ZLinkActorContext context,
        ZLinkMessage state,
        CancellationToken cancellationToken);
}
