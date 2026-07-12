package systems.zlink.framework.locations;

import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;

public interface ZLinkOwnerLeaseStore {
    CompletionStage<ZLinkOwnerLeaseRenewal> renewOwnerLease(
        String ownerId,
        RoutingId nodeRid,
        Duration leaseTtl);

    CompletionStage<Boolean> removeOwnerLease(String ownerId);

    CompletionStage<ZLinkOwnerLeaseSnapshot> listOwnerLeases();
}
