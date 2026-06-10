package systems.zlink.contract;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import systems.zlink.TestSupport;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.eventing.ZlinkDispatchOptions;
import systems.zlink.contracts.eventing.ZlinkNativeDispatcher;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.PairSocket;

public class NativeDispatcherContractTest {
    @Test
    public void dispatcherPollsSocketAndRunsHandlerOnCallbackExecutor() throws Exception {
        TestSupport.assumeNative();

        ExecutorService callbacks = Executors.newSingleThreadExecutor(task ->
            new Thread(task, "zlink-dispatch-callback"));
        try (Context context = Zlink.createContext();
             PairSocket server = context.createPairSocket();
             PairSocket client = context.createPairSocket();
             ZlinkNativeDispatcher dispatcher = ZlinkNativeDispatcher.create(
                 ZlinkDispatchOptions.builder()
                     .pollTimeout(Duration.ofMillis(10))
                     .callbackExecutor(callbacks)
                     .build())) {
            String endpoint = TestSupport.inprocEndpoint("native-dispatcher");
            server.bind(endpoint);
            client.connect(endpoint);

            CompletableFuture<String> payload = new CompletableFuture<>();
            AtomicReference<String> callbackThread = new AtomicReference<>();
            dispatcher.register(server, received -> {
                callbackThread.set(Thread.currentThread().getName());
                try (received) {
                    payload.complete(received.parts().getFirst().toUtf8String());
                }
                return CompletableFuture.completedFuture(null);
            });

            dispatcher.start().toCompletableFuture().get(1, TimeUnit.SECONDS);
            try (Message outbound = Message.from("ready")) {
                client.send().message(outbound).submit();
            }

            assertEquals("ready", payload.get(2, TimeUnit.SECONDS));
            assertEquals("zlink-dispatch-callback", callbackThread.get());
            assertTrue(dispatcher.isRunning());

            dispatcher.stop().toCompletableFuture().get(2, TimeUnit.SECONDS);
            assertFalse(dispatcher.isRunning());
        } finally {
            callbacks.shutdownNow();
        }
    }
}
