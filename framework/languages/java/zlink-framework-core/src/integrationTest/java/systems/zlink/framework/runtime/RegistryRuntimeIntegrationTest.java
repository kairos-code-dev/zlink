package systems.zlink.framework.runtime;

import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.net.ServerSocket;
import java.time.Duration;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.registry.ZLinkRegistryQueryFilter;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;

final class RegistryRuntimeIntegrationTest {
    private static final AtomicInteger NEXT_PORT =
        new AtomicInteger(30_000 + (int) (ProcessHandle.current().pid() % 10_000));

    @Test
    void embeddedRegistryQuerySmokeUsesJavaBindingPublicApi() {
        Zlink.version();
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        options.setPubEndpoint(tcpEndpoint());
        options.setRouterEndpoint(tcpEndpoint());

        try (ZLinkRegistryRuntime runtime = new ZLinkRegistryRuntime(
                 options,
                 new ZLinkJavaBackendAdapterFactory(),
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)))) {
            assertTrue(runtime.statusAsync()
                .toCompletableFuture()
                .join()
                .topologyEntryCount() >= 0);
            assertTrue(runtime.serviceSummaryAsync(ZLinkRegistryQueryFilter.all())
                .toCompletableFuture()
                .join()
                .isEmpty());
            assertTrue(runtime.topologyAsync(null)
                .toCompletableFuture()
                .join()
                .isEmpty());
        }
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
}
