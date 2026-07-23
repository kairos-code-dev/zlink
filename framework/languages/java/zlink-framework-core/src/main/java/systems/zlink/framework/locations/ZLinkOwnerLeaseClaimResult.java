package systems.zlink.framework.locations;

public sealed interface ZLinkOwnerLeaseClaimResult
    permits ZLinkOwnerLeaseClaimed,
        ZLinkOwnerLeaseClaimConflict,
        ZLinkOwnerLeaseGenerationExhausted {
}
