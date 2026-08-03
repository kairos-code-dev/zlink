package systems.zlink.framework.runtime.spots;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;
import systems.zlink.framework.configuration.ZLinkSpotRelocationReadinessMode;
import systems.zlink.framework.configuration.ZLinkUserSpotExecutionMode;

final class ZLinkDefaultSpotContextTest {
    @Test
    void lifecycleYieldIsLimitedToSharedSpotExecutions() {
        assertTrue(lifecycleYieldAllowed(
            ZLinkUserSpotExecutionMode.SPOT_WIDE,
            false));
        assertFalse(lifecycleYieldAllowed(
            ZLinkUserSpotExecutionMode.PER_ACTOR,
            false));
        assertTrue(lifecycleYieldAllowed(
            ZLinkUserSpotExecutionMode.PER_ACTOR,
            true));
    }

    @Test
    void lifecycleRelocationReadyFollowsSpotFactoryPolicy() {
        assertTrue(DefaultSpotContext.relocationReadyAllowed(
            ZLinkUserSpotExecutionMode.SPOT_WIDE,
            ZLinkSpotRelocationReadinessMode.APPLICATION_SIGNALED,
            false));
        assertFalse(DefaultSpotContext.relocationReadyAllowed(
            ZLinkUserSpotExecutionMode.SPOT_WIDE,
            ZLinkSpotRelocationReadinessMode.ANY_TURN_BOUNDARY,
            false));
        assertFalse(DefaultSpotContext.relocationReadyAllowed(
            ZLinkUserSpotExecutionMode.PER_ACTOR,
            ZLinkSpotRelocationReadinessMode.APPLICATION_SIGNALED,
            false));
        assertFalse(DefaultSpotContext.relocationReadyAllowed(
            ZLinkUserSpotExecutionMode.SPOT_WIDE,
            ZLinkSpotRelocationReadinessMode.APPLICATION_SIGNALED,
            true));
    }

    private static boolean lifecycleYieldAllowed(
        ZLinkUserSpotExecutionMode executionMode,
        boolean instanceSpot) {
        return DefaultSpotContext.lifecycleYieldAllowed(
            executionMode,
            instanceSpot);
    }
}
