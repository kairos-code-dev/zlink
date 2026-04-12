package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEvent;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PairSocket;
import dev.kairoscode.zlink.RecvException;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.RecvResult;
import dev.kairoscode.zlink.TestSupport;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class MonitorBehaviorContractTest {
    @Test
    public void blockingRecvReturnsObservedLifecycleEvent() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket server = new PairSocket(ctx);
             PairSocket client = new PairSocket(ctx);
             var serverMonitor = server.monitorOpen(
               MonitorEventType.LISTENING, MonitorEventType.ACCEPTED,
               MonitorEventType.CONNECTED)) {
            CompletableFuture<MonitorEvent> eventFuture =
                CompletableFuture.supplyAsync(serverMonitor::recv);

            String endpoint = TestSupport.tcpEndpoint();
            server.bind(endpoint);
            client.connect(endpoint);
            try (Message payload = Message.copyOfUtf8("monitor")) {
                client.send(payload);
            }

            MonitorEvent event = eventFuture.get(TestSupport.DEFAULT_TIMEOUT_MS,
                TimeUnit.MILLISECONDS);
            assertTrue(event.event() == MonitorEventType.LISTENING.getValue()
                || event.event() == MonitorEventType.ACCEPTED.getValue()
                || event.event() == MonitorEventType.CONNECTED.getValue());
        }
    }

    @Test
    public void tryRecvReturnsEmptyWhenNoMonitorEventExists() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket server = new PairSocket(ctx);
             PairSocket client = new PairSocket(ctx);
             var monitor = server.monitorOpen(MonitorEventType.LISTENING)) {
            String endpoint = TestSupport.tcpEndpoint();
            server.bind(endpoint);
            client.connect(endpoint);

            MonitorEvent event = monitor.recv();
            assertEquals(MonitorEventType.LISTENING.getValue(), event.event());
            RecvException ex = assertThrows(RecvException.class,
                () -> monitor.recv(RecvFlags.DONT_WAIT));
            assertEquals(RecvResult.NO_DATA, ex.getResult());
        }
    }
}
