package systems.zlink.framework;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.Flow;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkMeshChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkMeshNodeRuntimeOptions;
import systems.zlink.framework.channels.ZLinkRouteMeshRuntimeOptions;
import systems.zlink.framework.monitoring.ZLinkLocationRuntimeSnapshot;
import systems.zlink.framework.monitoring.ZLinkActivationConcurrency;
import systems.zlink.framework.monitoring.ZLinkInstanceSpotTypeSnapshot;
import systems.zlink.framework.monitoring.ZLinkMeshChannelSnapshot;
import systems.zlink.framework.monitoring.ZLinkMeshClaimSnapshot;
import systems.zlink.framework.monitoring.ZLinkMeshDrainResult;
import systems.zlink.framework.monitoring.ZLinkMeshDrained;
import systems.zlink.framework.monitoring.ZLinkMeshForceStopped;
import systems.zlink.framework.monitoring.ZLinkMeshNodeSnapshot;
import systems.zlink.framework.monitoring.ZLinkMeshNodeState;
import systems.zlink.framework.monitoring.ZLinkRouteMeshRuntime;
import systems.zlink.framework.locations.ZLinkCapacityUsage;
import systems.zlink.framework.locations.ZLinkMeshNodeObjectRole;
import systems.zlink.framework.locations.ZLinkPlacementCapacity;

final class RuntimeMonitoringContractTest {
    @Test
    void routeMeshRuntimeMatchesExactPublicMethodShape() throws Exception {
        assertEquals(
            ZLinkMeshNodeSnapshot.class,
            ZLinkRouteMeshRuntime.class
                .getMethod("snapshot", String.class)
                .getReturnType());
        assertEquals(
            Flow.Publisher.class,
            ZLinkRouteMeshRuntime.class
                .getMethod("observe", String.class, int.class)
                .getReturnType());
        assertEquals(
            boolean.class,
            ZLinkRouteMeshRuntime.class
                .getMethod("isReady", String.class)
                .getReturnType());
        assertEquals(
            CompletionStage.class,
            ZLinkRouteMeshRuntime.class
                .getMethod("drain", String.class, Duration.class)
                .getReturnType());
        assertEquals(
            CompletionStage.class,
            ZLinkRouteMeshRuntime.class
                .getMethod("awaitDrained", String.class)
                .getReturnType());

        assertTrue(ZLinkMeshDrainResult.class.isSealed());
        assertEquals(
            Set.of(ZLinkMeshDrained.class, ZLinkMeshForceStopped.class),
            Set.of(ZLinkMeshDrainResult.class.getPermittedSubclasses()));
    }

    @Test
    void routeMeshRuntimeOptionsMatchExactPublicMethodShape() throws Exception {
        assertEquals(
            ZLinkMeshNodeRuntimeOptions.class,
            ZLinkRouteMeshRuntimeOptions.class
                .getMethod("meshNode", String.class)
                .getReturnType());
        assertEquals(
            ZLinkMeshChannelRuntimeOptions.class,
            ZLinkRouteMeshRuntimeOptions.class
                .getMethod("channel", String.class, String.class)
                .getReturnType());
    }

    @Test
    void meshNodeSnapshotDefensivelyCopiesCollectionInputs() {
        ArrayList<String> sources = new ArrayList<>(List.of("manual"));
        ArrayList<ZLinkMeshChannelSnapshot> channels = new ArrayList<>(
            List.of(new ZLinkMeshChannelSnapshot("channel", 100, 1, true)));
        ArrayList<ZLinkInstanceSpotTypeSnapshot> instanceSpots = new ArrayList<>(
            List.of(new ZLinkInstanceSpotTypeSnapshot(
                "room", 2, 1, 0, 3, 64, Optional.of("activated"))));
        ZLinkMeshNodeSnapshot snapshot = new ZLinkMeshNodeSnapshot(
            "mesh",
            RoutingId.from("node"),
            1,
            1,
            "inproc://mesh",
            ZLinkMeshNodeState.SERVING,
            1,
            Instant.now(),
            sources,
            List.of(),
            channels,
            instanceSpots,
            new ZLinkMeshClaimSnapshot(true, 0, true, 0),
            new ZLinkLocationRuntimeSnapshot(
                "ready", Optional.empty(), Optional.empty()),
            ZLinkMeshNodeObjectRole.SERVER,
            100,
            new ZLinkPlacementCapacity(
                new ZLinkCapacityUsage(2, 1, 0),
                new ZLinkCapacityUsage(3, 1, 8),
                List.of()),
            new ZLinkActivationConcurrency(1, 4),
            List.of(),
            0,
            Optional.empty());

        sources.clear();
        channels.clear();
        instanceSpots.clear();

        assertEquals(List.of("manual"), snapshot.descriptorSources());
        assertEquals(1, snapshot.channels().size());
        assertEquals(1, snapshot.instanceSpots().size());
        assertEquals(0, snapshot.objectCapacity().actors().limit());
        assertEquals(new ZLinkActivationConcurrency(1, 4), snapshot.activationConcurrency());
        assertThrows(
            UnsupportedOperationException.class,
            () -> snapshot.descriptorSources().add("redis"));
    }
}
