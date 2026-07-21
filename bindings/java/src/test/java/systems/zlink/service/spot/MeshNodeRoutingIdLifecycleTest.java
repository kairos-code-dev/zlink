package systems.zlink.service.spot;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import java.time.Duration;
import java.util.List;
import java.util.UUID;
import org.junit.jupiter.api.Test;
import systems.zlink.TestSupport;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.errors.ZlinkException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.MeshNodeOptions;
import systems.zlink.contracts.service.spot.MeshNodeState;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;

class MeshNodeRoutingIdLifecycleTest {
    @Test
    void zeroMembershipNodeUsesNodePeerAndShutdownSurfaces() {
        TestSupport.assumeNative();

        String suffix = UUID.randomUUID().toString();
        try (Context context = Zlink.createContext();
             MeshNode node = context.createMeshNode(
                 new MeshNodeOptions("java-zero-membership-" + suffix, null))) {
            node.setRoutingId(RoutingId.from("zero-node-" + suffix));
            node.setBind("inproc://java-zero-membership-" + suffix);

            // No addChannel call: a caller-only MeshNode is valid.
            node.start();
            assertEquals(MeshNodeState.READY, node.status().state());
            assertEquals(0, node.status().channelCount());

            long intent = node.connectPeer("inproc://java-zero-peer-" + suffix);
            assertTrue(intent != 0);
            assertEquals(1, node.peers().size());
            assertEquals(0, node.peers().get(0).channelCount());

            try (Message payload = Message.from("zero-direct")) {
                ZlinkSubmitException error = assertThrows(
                    ZlinkSubmitException.class,
                    () -> node.sendToNode(
                        RoutingId.from("missing-node"), List.of(payload),
                        SendFlags.NONE));
                assertEquals(SubmitResult.NOT_CONNECTED, error.getResult());
            }

            node.shutdown(Duration.ofSeconds(1));
            assertEquals(MeshNodeState.STOPPED, node.status().state());
        }
    }

    @Test
    void fixedRoutingIdIsAppliedBeforeStartAndCannotChangeAfterStart() {
        TestSupport.assumeNative();

        String suffix = UUID.randomUUID().toString();
        RoutingId routingId = RoutingId.from("java-mesh-" + suffix);
        try (Context context = Zlink.createContext();
             MeshNode node = context.createMeshNode(
                 new MeshNodeOptions("routing-id-lifecycle", null))) {
            node.setRoutingId(routingId);
            node.setBind("inproc://java-mesh-" + suffix);
            node.addChannel("orders");

            assertEquals(routingId, node.getRoutingId());

            node.start();

            assertEquals(routingId, node.status().routingId());
            assertThrows(
                ZlinkException.class,
                () -> node.setRoutingId(RoutingId.from("changed")));
        }
    }

}
