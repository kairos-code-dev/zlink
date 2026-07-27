package systems.zlink.framework.kotlin

import java.time.Instant
import java.util.Optional
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.locations.ZLinkCapacityUsage
import systems.zlink.framework.locations.ZLinkMeshNodeObjectRole
import systems.zlink.framework.locations.ZLinkPlacementCapacity
import systems.zlink.framework.locations.ZLinkPlacementObjectKind
import systems.zlink.framework.locations.ZLinkSpotTypeCapacity
import systems.zlink.framework.locations.ZLinkActivationConcurrency
import systems.zlink.framework.monitoring.ZLinkLocationRuntimeSnapshot
import systems.zlink.framework.monitoring.ZLinkMeshClaimSnapshot
import systems.zlink.framework.monitoring.ZLinkMeshNodeSnapshot
import systems.zlink.framework.monitoring.ZLinkMeshNodeState

class KotlinCapacityMonitoringContractTest {
    @Test
    fun `Java capacity projection keeps the exact Kotlin-visible shape`() {
        val capacity = ZLinkPlacementCapacity(
            ZLinkCapacityUsage(3, 1, 0),
            ZLinkCapacityUsage(2, 1, 12),
            listOf(
                ZLinkSpotTypeCapacity(
                    ZLinkPlacementObjectKind.INSTANCE_SPOT,
                    "room",
                    ZLinkCapacityUsage(2, 1, 5),
                ),
            ),
        )
        val snapshot = ZLinkMeshNodeSnapshot(
            "mesh",
            RoutingId.from("node"),
            1,
            2,
            "inproc://mesh",
            ZLinkMeshNodeState.SERVING,
            1,
            Instant.now(),
            emptyList(),
            emptyList(),
            emptyList(),
            emptyList(),
            ZLinkMeshClaimSnapshot(true, 0, true, 0),
            ZLinkLocationRuntimeSnapshot("ready", Optional.empty(), Optional.empty()),
            ZLinkMeshNodeObjectRole.SERVER,
            100,
            capacity,
            ZLinkActivationConcurrency(2, 8),
            emptyList(),
            0,
            Optional.empty(),
        )

        assertEquals(3, snapshot.objectCapacity().actors().active())
        assertEquals(0, snapshot.objectCapacity().actors().limit())
        assertEquals(5, snapshot.objectCapacity().spotTypes().first().usage().limit())
        assertEquals(2, snapshot.activationConcurrency().active())
        assertEquals(8, snapshot.activationConcurrency().limit())
    }
}
