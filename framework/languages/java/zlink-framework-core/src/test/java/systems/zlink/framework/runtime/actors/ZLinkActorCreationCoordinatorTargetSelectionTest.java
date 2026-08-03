package systems.zlink.framework.runtime.actors;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkActivationConcurrency;
import systems.zlink.framework.locations.ZLinkCapacityUsage;
import systems.zlink.framework.locations.ZLinkMeshNodeObjectRole;
import systems.zlink.framework.locations.ZLinkObjectCapability;
import systems.zlink.framework.locations.ZLinkObjectMaintenancePolicyKind;
import systems.zlink.framework.locations.ZLinkPlacementCapacity;
import systems.zlink.framework.locations.ZLinkPlacementObjectKind;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState;
import systems.zlink.framework.runtime.internal.locations.ZLinkMeshNodeDescriptor;

final class ZLinkActorCreationCoordinatorTargetSelectionTest {
    private static final Instant NOW = Instant.parse(
        "2026-01-01T00:00:00Z");

    @Test
    void localServingTargetIsPreferredBeforeWeightedRemoteSelection() {
        RoutingId local = RoutingId.from("local-node");
        ZLinkMeshNodeDescriptor remote = descriptor(
            RoutingId.from("remote-node"), 10_000);
        ZLinkMeshNodeDescriptor localDescriptor = descriptor(local, 1);

        Optional<ZLinkMeshNodeDescriptor> selected =
            ZLinkActorCreationCoordinator.localCandidate(
                List.of(remote, localDescriptor), local);

        assertTrue(selected.isPresent());
        assertEquals(local, selected.orElseThrow().rid());
    }

    @Test
    void noLocalTargetLeavesWeightedSelectionAvailable() {
        RoutingId local = RoutingId.from("local-node");

        Optional<ZLinkMeshNodeDescriptor> selected =
            ZLinkActorCreationCoordinator.localCandidate(
                List.of(descriptor(
                    RoutingId.from("remote-node"), 100)), local);

        assertTrue(selected.isEmpty());
    }

    private static ZLinkMeshNodeDescriptor descriptor(
        RoutingId rid,
        int placementWeight) {
        return new ZLinkMeshNodeDescriptor(
            "mesh",
            rid,
            1,
            1,
            "tcp://127.0.0.1:1",
            Map.of(),
            1,
            List.of(new ZLinkObjectCapability(
                ZLinkPlacementObjectKind.ACTOR,
                "player",
                ZLinkObjectMaintenancePolicyKind.SNAPSHOT,
                true,
                0)),
            ZLinkMeshNodeObjectRole.SERVER,
            Optional.of("entry-00000000-0000-4000-8000-000000000001"),
            placementWeight,
            new ZLinkPlacementCapacity(
                new ZLinkCapacityUsage(0, 0, 8),
                new ZLinkCapacityUsage(0, 0, 0),
                List.of()),
            new ZLinkActivationConcurrency(0, 8),
            Optional.empty(),
            ZLinkFrameworkRuntimeState.SERVING,
            "security",
            "owner",
            1,
            NOW);
    }
}
