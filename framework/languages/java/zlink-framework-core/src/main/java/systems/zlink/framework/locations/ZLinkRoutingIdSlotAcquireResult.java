package systems.zlink.framework.locations;

public sealed interface ZLinkRoutingIdSlotAcquireResult
    permits ZLinkRoutingIdSlotAcquired,
            ZLinkRoutingIdSlotGroupExhausted,
            ZLinkRoutingIdSlotGroupConfigurationMismatch,
            ZLinkRoutingIdSlotIdentityModeConflict {
}
