package systems.zlink.service.spot;

import static org.junit.jupiter.api.Assertions.assertEquals;

import org.junit.jupiter.api.Test;
import systems.zlink.TestSupport;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.MeshNodeOptions;

final class MeshNodeMaxMessageSizeTest {
    @Test
    void commonMaxMessageSizeOptionAppliesToMeshNodeHandle() {
        TestSupport.assumeNative();

        try (Context context = Zlink.createContext();
             MeshNode node = context.createMeshNode(
                 new MeshNodeOptions("max-message-size", null))) {
            node.setMaxMessageSize(4096);
            assertEquals(4096, node.maxMessageSize());

            node.setMaxMessageSize(-1);
            assertEquals(-1, node.maxMessageSize());
        }
    }
}
