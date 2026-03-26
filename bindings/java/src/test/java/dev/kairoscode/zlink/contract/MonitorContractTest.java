package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertTrue;

public class MonitorContractTest {
    @Test
    public void monitorSnapshotUsesCanonicalMonitorSurface() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket socket = new Socket(ctx, SocketType.PAIR);
             var monitor = socket.monitorOpen(MonitorEventType.ALL.getValue())) {
            assertTrue(monitor.snapshot().readyCount() >= 0);
        }
    }
}
