package systems.zlink.framework;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicBoolean;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

final class ZLinkFrameworkFacadeTest {
    private static final AtomicBoolean VIRTUAL_HANDLER_THREAD = new AtomicBoolean();

    @Test
    void publicFacadeStartsFrameworkWithoutExposingBackendRuntimeTypes() {
        String endpoint = "inproc://zlink-java-facade-" + UUID.randomUUID();

        try (ZLinkFramework framework = ZLinkFramework.start(options ->
                 options.addClientServerChannel("profile", channel -> {
                     channel.enableServer(server -> server.bind(endpoint));
                     channel.enableClient(client -> client.useManualConnections(
                         endpoints -> endpoints.connect(endpoint)));
                     channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo");
                 }))) {
            String reply = framework.client()
                .requestToChannel("profile", "hello")
                .packetName("Echo")
                .submitAsync(String.class)
                .toCompletableFuture()
                .join();

            assertEquals("hello", reply);
        }
    }

    @Test
    void channelHandlersRunOnVirtualThreadExecutorByDefault() {
        VIRTUAL_HANDLER_THREAD.set(false);
        String endpoint = "inproc://zlink-java-handler-executor-" + UUID.randomUUID();

        try (ZLinkFramework framework = ZLinkFramework.start(options ->
                 options.addClientServerChannel("profile", channel -> {
                     channel.enableServer(server -> server.bind(endpoint));
                     channel.enableClient(client -> client.useManualConnections(
                         endpoints -> endpoints.connect(endpoint)));
                     channel.addRequestHandler(VirtualThreadEchoHandler.class, String.class, String.class, "Echo");
                 }))) {
            String reply = framework.client()
                .requestToChannel("profile", "hello")
                .packetName("Echo")
                .submitAsync(String.class)
                .toCompletableFuture()
                .join();

            assertEquals("hello", reply);
            assertTrue(VIRTUAL_HANDLER_THREAD.get());
        }
    }

    public static final class EchoHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(String request, ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }

    public static final class VirtualThreadEchoHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(String request, ZLinkRequestContext context) {
            VIRTUAL_HANDLER_THREAD.set(Thread.currentThread().isVirtual());
            return CompletableFuture.completedFuture(request);
        }
    }
}
