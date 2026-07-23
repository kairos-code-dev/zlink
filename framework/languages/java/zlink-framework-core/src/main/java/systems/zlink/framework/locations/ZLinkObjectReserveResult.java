package systems.zlink.framework.locations;

public sealed interface ZLinkObjectReserveResult
    permits ZLinkObjectReserved, ZLinkObjectConflict,
        ZLinkObjectAlreadyExists, ZLinkObjectTypeMismatch,
        ZLinkPlacementCapacityExhausted,
        ZLinkObjectGenerationExhausted {
}
