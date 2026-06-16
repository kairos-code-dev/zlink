package systems.zlink.samples.deliverydispatch.server.dispatchapi.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.configuration.EvidenceStore;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages.Status;

@ZLinkHandlerGroup("api")
public final class ServerAssertionHandler
    implements ZLinkRequestHandler<Messages.ServerAssertionReq, Messages.ServerAssertionRes> {
    private final EvidenceStore evidence;

    public ServerAssertionHandler(EvidenceStore evidence) {
        this.evidence = evidence;
    }

    @Override
    public Messages.ServerAssertionRes handle(
        Messages.ServerAssertionReq request,
        ZLinkRequestContext context) {
        boolean success = evidence.hasSequence(
            request.successfulDeliveryId(),
            Status.Assigned,
            Status.Accepted,
            Status.PickedUp,
            Status.Delivered);
        boolean reassigned = evidence.hasSequence(
            request.reassignedDeliveryId(),
            Status.Assigned,
            Status.Reassigned,
            Status.Accepted,
            Status.PickedUp,
            Status.Delivered);
        return new Messages.ServerAssertionRes(success && reassigned, evidence.readLines());
    }
}
