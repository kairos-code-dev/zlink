package systems.zlink.contract;

import systems.zlink.TestSupport;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.sockets.PairSocket;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertTrue;

public class MonitorContractTest {
    @Test
    public void monitorStatusUsesCanonicalMonitorSurface() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket socket = new PairSocket(ctx);
             var monitor = socket.monitorOpen()) {
            assertTrue(monitor.status().sndPendingMsgs() >= 0L);
        }
    }
}
