package systems.zlink.framework.locations;

import java.time.Duration;
import java.util.concurrent.CompletionStage;

public interface ZLinkOwnerLeaseStore {
    CompletionStage<ZLinkOwnerLeaseClaimResult> claimOwnerLease(
        String ownerId,
        Duration leaseTtl);

    CompletionStage<ZLinkOwnerLeaseReadResult> readOwnerLease(
        String ownerId);

    CompletionStage<ZLinkOwnerLeaseRenewResult> renewOwnerLease(
        ZLinkLocationOwnerToken token,
        Duration leaseTtl);

    CompletionStage<ZLinkOwnerLeaseReleaseResult> releaseOwnerLease(
        ZLinkLocationOwnerToken token);
}
