package systems.zlink.framework.runtime.service;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;

final class ZLinkM6ARuntimeContractTest {
    @Test
    void topologyFencesStaleConnectionsAndSelectsOnlyServingPositiveWeight() {
        var topology = new ZLinkServiceTopologyRegistry(
            descriptor("mesh", "local", 1, 1, List.of(), 100));
        var first = descriptor(
            "mesh",
            "peer-a",
            9,
            2,
            List.of(new ZLinkServiceNodeDescriptor.Channel("orders", 0)),
            100);
        var updated = descriptor(
            "mesh",
            "peer-a",
            9,
            3,
            List.of(new ZLinkServiceNodeDescriptor.Channel("orders", 7)),
            100);

        assertEquals(
            ZLinkServiceTopologyRegistry.AdmissionResult.ADMITTED,
            topology.admit(first, "pipe-old"));
        assertTrue(topology.selectChannel("orders").isEmpty());
        assertEquals(
            ZLinkServiceTopologyRegistry.AdmissionResult.ADMITTED,
            topology.admit(updated, "pipe-current"));
        assertEquals(
            RoutingId.from("peer-a"),
            topology.selectChannel("orders")
                .orElseThrow()
                .descriptor()
                .nodeRoutingId());
        assertFalse(topology.disconnect(RoutingId.from("peer-a"), "pipe-old"));
        assertTrue(topology.peer(RoutingId.from("peer-a")).isPresent());
        assertTrue(topology.disconnect(
            RoutingId.from("peer-a"), "pipe-current"));
    }

    @Test
    void commonWeightsUseExactRangeRatioRevisionAndCapacityEligibility() {
        assertEquals(
            10_000,
            new ZLinkServiceNodeDescriptor.Channel(
                "maximum",
                10_000).weight());
        org.junit.jupiter.api.Assertions.assertThrows(
            IllegalArgumentException.class,
            () -> new ZLinkServiceNodeDescriptor.Channel("negative", -1));
        org.junit.jupiter.api.Assertions.assertThrows(
            IllegalArgumentException.class,
            () -> new ZLinkServiceNodeDescriptor.Channel(
                "too-large",
                10_001));

        var topology = new ZLinkServiceTopologyRegistry(
            descriptor("mesh", "local", 1, 1, List.of(), 100));
        topology.admit(
            descriptor(
                "mesh",
                "peer-a",
                1,
                1,
                List.of(new ZLinkServiceNodeDescriptor.Channel(
                    "orders",
                    100)),
                100),
            "pipe-a");
        topology.admit(
            descriptor(
                "mesh",
                "peer-b",
                1,
                1,
                List.of(new ZLinkServiceNodeDescriptor.Channel(
                    "orders",
                    300)),
                300),
            "pipe-b");
        int selectedA = 0;
        int selectedB = 0;
        for (int index = 0; index < 400; index++) {
            String rid = topology.selectChannel("orders")
                .orElseThrow()
                .descriptor()
                .nodeRoutingId()
                .toString();
            if ("peer-a".equals(rid)) {
                selectedA++;
            } else if ("peer-b".equals(rid)) {
                selectedB++;
            }
        }
        assertEquals(100, selectedA);
        assertEquals(300, selectedB);

        assertEquals(
            ZLinkServiceTopologyRegistry.AdmissionResult.STALE_DESCRIPTOR,
            topology.admit(
                descriptor(
                    "mesh",
                    "peer-b",
                    1,
                    1,
                    List.of(new ZLinkServiceNodeDescriptor.Channel(
                        "orders",
                        0)),
                    0),
                "stale-pipe"));
        assertEquals(
            ZLinkServiceTopologyRegistry.AdmissionResult.ADMITTED,
            topology.admit(
                descriptor(
                    "mesh",
                    "peer-b",
                    1,
                    2,
                    List.of(new ZLinkServiceNodeDescriptor.Channel(
                        "orders",
                        0)),
                    0),
                "current-pipe"));
        for (int index = 0; index < 16; index++) {
            assertEquals(
                RoutingId.from("peer-a"),
                topology.selectChannel("orders")
                    .orElseThrow()
                    .descriptor()
                    .nodeRoutingId());
        }

        var capacityTopology = new ZLinkServiceTopologyRegistry(
            descriptor("mesh", "local", 1, 1, List.of(), 100));
        capacityTopology.admit(
            descriptorWithCapacity(
                "full", 10_000, 100, 10, 100, 10),
            "full-pipe");
        capacityTopology.admit(
            descriptorWithCapacity(
                "eligible", 1, 100, 0, 100, 0),
            "eligible-pipe");
        assertEquals(
            RoutingId.from("eligible"),
            capacityTopology.selectPlacement()
                .orElseThrow()
                .descriptor()
                .nodeRoutingId());
    }

    @Test
    void livenessResendsOneProbeAndOnlyMatchingAckRenewsDeadline() {
        var liveness = new ZLinkServiceLivenessRegistry(
            Duration.ofSeconds(5), Duration.ofSeconds(15));
        RoutingId peer = RoutingId.from("peer");
        liveness.admit(peer, "pipe", 0);

        var first = liveness.tick(Duration.ofSeconds(5).toNanos());
        assertEquals(1, first.probes().size());
        long probe = first.probes().getFirst().probeId();
        var retry = liveness.tick(Duration.ofSeconds(10).toNanos());
        assertEquals(probe, retry.probes().getFirst().probeId());
        assertFalse(liveness.acknowledge(
            peer, "stale-pipe", probe, Duration.ofSeconds(11).toNanos()));
        assertFalse(liveness.acknowledge(
            peer, "pipe", probe + 1, Duration.ofSeconds(11).toNanos()));
        assertTrue(liveness.acknowledge(
            peer, "pipe", probe, Duration.ofSeconds(11).toNanos()));

        assertTrue(liveness.tick(Duration.ofSeconds(20).toNanos())
            .timedOutNodes().isEmpty());
        assertEquals(
            List.of(peer),
            liveness.tick(Duration.ofSeconds(26).toNanos()).timedOutNodes());
    }

    @Test
    void mailboxSerializesEachOwnerAndKeepsInfrastructureReserve() {
        var mailbox = new ZLinkServiceMailbox(2, 8, 1, 8);
        assertTrue(mailbox.tryEnqueue(record(
            "node:a", ZLinkServiceMailbox.Domain.APPLICATION, 4)));
        assertTrue(mailbox.tryEnqueue(record(
            "node:a", ZLinkServiceMailbox.Domain.APPLICATION, 4)));
        assertFalse(mailbox.tryEnqueue(record(
            "node:b", ZLinkServiceMailbox.Domain.APPLICATION, 1)));
        assertTrue(mailbox.tryEnqueue(record(
            "peer:a", ZLinkServiceMailbox.Domain.INFRASTRUCTURE, 8)));

        var first = mailbox.tryClaim(
            ZLinkServiceMailbox.Domain.APPLICATION, 1, 4).orElseThrow();
        assertTrue(mailbox.tryClaim(
            ZLinkServiceMailbox.Domain.APPLICATION, 1, 4).isEmpty());
        assertTrue(mailbox.release(first));
        var second = mailbox.tryClaim(
            ZLinkServiceMailbox.Domain.APPLICATION, 1, 4).orElseThrow();
        assertEquals(first.owner(), second.owner());
        assertNotEquals(first.serial(), second.serial());
        assertTrue(mailbox.tryClaim(
            ZLinkServiceMailbox.Domain.INFRASTRUCTURE, 1, 8).isPresent());
    }

    @Test
    void locationAuthorityPublishesOpaqueCasChangesAndFencesOldVersion() {
        Instant storeNow = Instant.parse("2026-07-23T00:00:00Z");
        var authority = new ZLinkInMemoryLocationAuthority(() -> storeNow);
        List<ZLinkInMemoryLocationAuthority.Change> changes = new ArrayList<>();
        authority.subscribe(changes::add);

        var created = authority.compareExchange(
            "zla1:a:1:a",
            ZLinkInMemoryLocationAuthority.Expectation.expectMissing(),
            ZLinkInMemoryLocationAuthority.Mutation.newObject(
                new byte[] {1, 2, 3}));
        assertEquals(
            ZLinkInMemoryLocationAuthority.CasKind.STORED,
            created.kind());
        byte[] callerCopy = created.snapshot().payload();
        callerCopy[0] = 9;
        assertArrayEquals(
            new byte[] {1, 2, 3},
            authority.read("zla1:a:1:a").snapshot().payload());

        var moved = authority.compareExchange(
            "zla1:a:1:a",
            ZLinkInMemoryLocationAuthority.Expectation.version(
                created.snapshot().storeVersion()),
            ZLinkInMemoryLocationAuthority.Mutation.newOwner(
                new byte[] {4}));
        assertEquals(
            created.snapshot().objectGeneration(),
            moved.snapshot().objectGeneration());
        assertNotEquals(
            created.snapshot().authorityOwnerGeneration(),
            moved.snapshot().authorityOwnerGeneration());
        assertEquals(
            ZLinkInMemoryLocationAuthority.CasKind.CONFLICT,
            authority.compareExchange(
                "zla1:a:1:a",
                ZLinkInMemoryLocationAuthority.Expectation.version(
                    created.snapshot().storeVersion()),
                ZLinkInMemoryLocationAuthority.Mutation.preserve(
                    new byte[] {5}))
                .kind());
        assertEquals(2, changes.size());
    }

    private static ZLinkServiceMailbox.Record record(
        String owner,
        ZLinkServiceMailbox.Domain domain,
        int bytes) {
        return new ZLinkServiceMailbox.Record(
            owner,
            domain,
            List.of(new byte[bytes]),
            null,
            null,
            null);
    }

    private static ZLinkServiceNodeDescriptor descriptor(
        String meshName,
        String rid,
        long lifecycle,
        long revision,
        List<ZLinkServiceNodeDescriptor.Channel> channels,
        int placementWeight) {
        return new ZLinkServiceNodeDescriptor(
            meshName,
            RoutingId.from(rid),
            lifecycle,
            revision,
            "inproc://" + rid,
            channels,
            ZLinkServiceNodeDescriptor.State.SERVING,
            "default",
            4 * 1024 * 1024,
            0,
            List.of(ZLinkServiceNodeDescriptor.REQUIRED_CAPABILITY),
            ZLinkServiceNodeDescriptor.ObjectRole.SERVER,
            placementWeight,
            100,
            10,
            0,
            0);
    }

    private static ZLinkServiceNodeDescriptor descriptorWithCapacity(
        String rid,
        int placementWeight,
        int activeLimit,
        int activeUsed,
        int pendingLimit,
        int pendingUsed) {
        return new ZLinkServiceNodeDescriptor(
            "mesh",
            RoutingId.from(rid),
            1,
            1,
            "inproc://" + rid,
            List.of(),
            ZLinkServiceNodeDescriptor.State.SERVING,
            "default",
            4 * 1024 * 1024,
            0,
            List.of(ZLinkServiceNodeDescriptor.REQUIRED_CAPABILITY),
            ZLinkServiceNodeDescriptor.ObjectRole.SERVER,
            placementWeight,
            activeLimit,
            pendingLimit,
            activeUsed,
            pendingUsed);
    }
}
