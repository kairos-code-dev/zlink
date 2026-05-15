package systems.zlink.contract;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.MonitorEventType;
import systems.zlink.contracts.PairSocket;
import systems.zlink.contracts.TestSupport;
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
