package systems.zlink.integration.contract;

import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;
import systems.zlink.TestSupport;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.sockets.PairSocket;

class ServiceContractsIntegrationTest {
    @Test
    void monitorStatusExposesCanonicalMonitorState() {
        TestSupport.assumeNative();

        try (Context context = Zlink.createContext();
             PairSocket socket = context.createPairSocket();
             var monitor = socket.monitorOpen()) {
            assertTrue(monitor.status().sndPendingMsgs() >= 0L);
        }
    }
}
