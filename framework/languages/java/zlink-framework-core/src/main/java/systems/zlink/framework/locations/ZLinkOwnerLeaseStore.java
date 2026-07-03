package systems.zlink.framework.locations;

import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;

public interface ZLinkOwnerLeaseStore {
    CompletionStage<ZLinkLocationWriteResult> renewOwnerLeaseAsync(
        String ownerId,
        RoutingId nodeRid,
        Duration leaseTtl);

    CompletionStage<ZLinkLocationWriteResult> removeOwnerLeaseAsync(String ownerId);

    CompletionStage<ZLinkOwnerLeaseSnapshot> listOwnerLeasesAsync();
}
