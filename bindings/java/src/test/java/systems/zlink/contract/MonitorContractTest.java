package systems.zlink.contract;

import systems.zlink.Context;
import systems.zlink.MonitorEventType;
import systems.zlink.PairSocket;
import systems.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertTrue;

public class MonitorContractTest {
    @Test
    public void monitorSnapshotUsesCanonicalMonitorSurface() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket socket = new PairSocket(ctx);
             var monitor = socket.monitorOpen()) {
            assertTrue(monitor.snapshot().sndPendingMsgs() >= 0L);
        }
    }
}
