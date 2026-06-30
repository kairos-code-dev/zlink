package systems.zlink.e2e.discoveryregistryha.probe;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.time.Duration;
import java.util.concurrent.TimeUnit;
import org.springframework.context.SmartLifecycle;
import systems.zlink.e2e.discoveryregistryha.shared.Contracts;
import systems.zlink.e2e.discoveryregistryha.shared.HttpSupport;
import systems.zlink.e2e.discoveryregistryha.shared.Wait;
import systems.zlink.framework.registry.ZLinkRegistryQueryClient;

public final class ProbeEndpoints implements SmartLifecycle {
    private final ZLinkRegistryQueryClient query;
    private final ObjectMapper json;
    private final String endpoint;
    private HttpServer server;
    private boolean running;

    public ProbeEndpoints(
        ZLinkRegistryQueryClient query,
        ObjectMapper json,
        String endpoint) {
        this.query = query;
        this.json = json;
        this.endpoint = endpoint;
    }

    @Override
    public void start() {
        if (endpoint == null || endpoint.isBlank()) {
            return;
        }
        try {
            server = HttpSupport.createServer(endpoint);
            server.createContext("/health", exchange -> HttpSupport.writeText(exchange, "ok\n"));
            server.createContext("/shutdown", exchange -> {
                HttpSupport.writeJson(exchange, json, java.util.Map.of("status", "stopping"));
                HttpSupport.shutdownAsync();
            });
            server.createContext("/registry/topology", exchange ->
                HttpSupport.writeJson(exchange, json, await(query.topology())));
            server.createContext("/topology-rids", exchange ->
                HttpSupport.writeJson(exchange, json, topologyRids()));
            server.createContext("/registry/topology/wait", exchange -> {
                Contracts.TopologyReadyWaitReq request =
                    HttpSupport.readJson(exchange, json, Contracts.TopologyReadyWaitReq.class);
                java.util.List<?> topology = Wait.until(
                    Duration.ofMillis(Math.max(1, request.timeoutMilliseconds())),
                    "timed out waiting for topology ready count " + request.readyCount(),
                    () -> {
                        java.util.List<?> entries = await(query.topology()).stream()
                            .filter(entry -> Contracts.CHANNEL.equals(entry.channelName()))
                            .toList();
                        return entries.size() >= request.readyCount() ? entries : null;
                    });
                HttpSupport.writeJson(exchange, json, topology);
            });
            server.start();
            running = true;
        } catch (Exception error) {
            throw new IllegalStateException("failed to start probe endpoints " + endpoint, error);
        }
    }

    private java.util.List<String> topologyRids() {
        return await(query.topology()).stream()
            .filter(entry -> Contracts.CHANNEL.equals(entry.channelName()))
            .map(entry -> entry.routingId().toString())
            .sorted()
            .toList();
    }

    private static <T> T await(java.util.concurrent.CompletionStage<T> stage) {
        try {
            return stage.toCompletableFuture().get(3, TimeUnit.SECONDS);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("registry query interrupted", error);
        } catch (Exception error) {
            throw new IllegalStateException("registry query failed", error);
        }
    }

    @Override
    public void stop() {
        if (server != null) {
            server.stop(0);
            server = null;
        }
        running = false;
    }

    @Override
    public boolean isRunning() {
        return running;
    }
}
