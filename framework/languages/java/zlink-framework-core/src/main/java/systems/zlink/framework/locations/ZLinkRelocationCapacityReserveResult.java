package systems.zlink.framework.locations;

public sealed interface ZLinkRelocationCapacityReserveResult
    permits ZLinkRelocationCapacityReserved,
        ZLinkRelocationCapacityAlreadyReserved,
        ZLinkRelocationCapacityConflict,
        ZLinkRelocationCapacityTargetUnavailable,
        ZLinkRelocationCapacityExhausted {
}
