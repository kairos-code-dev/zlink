package systems.zlink.framework.runtime;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.io.IOException;
import java.net.ServerSocket;
import java.time.Duration;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;

final class ChannelMessagingTest {
    private static final AtomicInteger NEXT_PORT =
        new AtomicInteger(32_000 + (int) (ProcessHandle.current().pid() % 10_000));

    @Test
    void manualClientServer_requestReplySucceeds() {
        String endpoint = "inproc://zlink-java-profile-" + UUID.randomUUID();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind(endpoint));
            channel.enableClient(client ->
                client.useManualConnections(endpoints -> endpoints.connect(endpoint)));
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo");
        });

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory())) {
            String reply = runtime.client()
                .requestToChannel("profile", "hello")
                .packetName("Echo")
                .submitAsync(String.class)
                .toCompletableFuture()
                .join();

            assertEquals("hello", reply);
        }
    }

    @Test
    void discoveryClientServer_requestReplySucceeds() {
        String registryPub = tcpEndpoint();
        String registryRouter = tcpEndpoint();
        String serverEndpoint = tcpEndpoint();
        ZLinkEmbeddedRegistryOptions registryOptions = new ZLinkEmbeddedRegistryOptions();
        registryOptions.setPubEndpoint(registryPub);
        registryOptions.setRouterEndpoint(registryRouter);

        DefaultZLinkFrameworkOptions serverOptions = new DefaultZLinkFrameworkOptions();
        serverOptions.useDiscovery(registry -> registry.add(registryRouter));
        serverOptions.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind(serverEndpoint));
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo");
        });

        DefaultZLinkFrameworkOptions clientOptions = new DefaultZLinkFrameworkOptions();
        clientOptions.setDefaultTimeout(Duration.ofMillis(100));
        clientOptions.useDiscovery(registry -> registry.add(registryRouter));
        clientOptions.addClientServerChannel("profile", channel -> channel.enableClient());

        try (ZLinkRegistryRuntime ignoredRegistry = new ZLinkRegistryRuntime(
                 registryOptions,
                 new ZLinkJavaBackendAdapterFactory(),
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
             ZLinkFrameworkRuntime ignoredServer =
                 ZLinkFrameworkRuntime.start(serverOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime client =
                 ZLinkFrameworkRuntime.start(clientOptions, new ZLinkJavaBackendAdapterFactory())) {
            assertEquals("hello", awaitDiscoveryReply(client));
        }
    }

    private static String awaitDiscoveryReply(ZLinkFrameworkRuntime client) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                return client.client()
                    .requestToChannel("profile", "hello")
                    .packetName("Echo")
                    .timeout(Duration.ofMillis(100))
                    .submitAsync(String.class)
                    .toCompletableFuture()
                    .join();
            } catch (RuntimeException ex) {
                lastFailure = ex;
                Thread.onSpinWait();
            }
        }
        throw new AssertionError("discovery request did not succeed", lastFailure);
    }

    private static String tcpEndpoint() {
        return "tcp://127.0.0.1:" + nextPort();
    }

    private static int nextPort() {
        for (int attempt = 0; attempt < 200; attempt++) {
            int port = NEXT_PORT.getAndIncrement();
            if (isBindable(port)) {
                return port;
            }
        }
        throw new IllegalStateException("failed to allocate tcp port");
    }

    private static boolean isBindable(int port) {
        try (ServerSocket server = new ServerSocket(port)) {
            server.setReuseAddress(false);
            return true;
        } catch (IOException ignored) {
            return false;
        }
    }

    public static final class EchoHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(String request, ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }
}
