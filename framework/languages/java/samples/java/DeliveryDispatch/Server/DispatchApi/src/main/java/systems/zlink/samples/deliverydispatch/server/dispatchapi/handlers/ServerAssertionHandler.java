package systems.zlink.samples.deliverydispatch.server.dispatchapi.handlers;

import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RestController;
import systems.zlink.samples.deliverydispatch.server.configuration.EvidenceStore;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages.Status;

@RestController
public final class ServerAssertionHandler {
    private final EvidenceStore evidence;

    public ServerAssertionHandler(EvidenceStore evidence) {
        this.evidence = evidence;
    }

    @PostMapping("/self-check/assert")
    public Messages.ServerAssertionRes handle(@RequestBody Messages.ServerAssertionReq request) {
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
