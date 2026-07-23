package systems.zlink.framework.locations;

public sealed interface ZLinkAuthorityWriteResult
    permits ZLinkAuthorityStored, ZLinkAuthorityDeleted,
        ZLinkAuthorityConflict, ZLinkAuthorityGenerationExhausted {
}
