package systems.zlink.service.spot;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.util.UUID;
import org.junit.jupiter.api.Test;
import systems.zlink.TestSupport;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.errors.ZlinkException;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.MeshNodeOptions;

class MeshNodeRoutingIdLifecycleTest {
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
