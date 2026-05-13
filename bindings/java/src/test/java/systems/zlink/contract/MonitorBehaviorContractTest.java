package systems.zlink.contract;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.MonitorEvent;
import systems.zlink.MonitorEventType;
import systems.zlink.MonitorSocket;
import systems.zlink.PairSocket;
import systems.zlink.TestSupport;
import java.lang.reflect.Modifier;
import java.lang.reflect.Method;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
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
                client.send().message(payload).submit();
            }

            MonitorEvent event = eventFuture.get(TestSupport.DEFAULT_TIMEOUT_MS,
                TimeUnit.MILLISECONDS);
            assertTrue(event.event() == MonitorEventType.LISTENING
                || event.event() == MonitorEventType.ACCEPTED
                || event.event() == MonitorEventType.CONNECTED);
        }
    }

    @Test
    public void monitorSocketExposesDocumentedRecvSurface() {
        assertTrue(hasPublicMethod(systems.zlink.MonitorSocket.class, "recv"));
        assertTrue(hasPublicMethod(systems.zlink.MonitorSocket.class, "recv",
            systems.zlink.RecvFlags.class));
        assertTrue(Modifier.isPublic(MonitorSocket.class.getModifiers()));
        assertNotNull(MonitorSocket.IGNORE_HANDLER);
    }

    private static boolean hasPublicMethod(Class<?> type, String name,
                                           Class<?>... parameterTypes) {
        try {
            Method method = type.getMethod(name, parameterTypes);
            return method != null;
        } catch (NoSuchMethodException ex) {
            return false;
        }
    }
}
