package systems.zlink.e2e.discoveryregistryha.registry;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.time.Duration;
import java.util.concurrent.TimeUnit;
import org.springframework.context.SmartLifecycle;
import systems.zlink.e2e.discoveryregistryha.shared.Contracts;
import systems.zlink.e2e.discoveryregistryha.shared.HttpSupport;
import systems.zlink.e2e.discoveryregistryha.shared.Wait;
import systems.zlink.framework.registry.ZLinkRegistryQuery;

public final class RegistryEndpoints implements SmartLifecycle {
    private final ZLinkRegistryQuery query;
    private final ObjectMapper json;
    private final String endpoint;
    private HttpServer server;
    private boolean running;

    public RegistryEndpoints(
        ZLinkRegistryQuery query,
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
            server.createContext("/status", exchange ->
                HttpSupport.writeJson(exchange, json, await(query.status())));
            server.createContext("/member-peers", exchange ->
                HttpSupport.writeJson(exchange, json, await(query.memberPeers(Contracts.CHANNEL)).stream()
                    .map(peer -> new MemberPeerDto(
                        peer.channelName(),
                        peer.serviceRole().name(),
                        peer.endpoint(),
                        peer.routingId().toString(),
                        peer.weight()))
                    .toList()));
            server.createContext("/topology", exchange ->
                HttpSupport.writeJson(exchange, json, await(query.topology())));
            server.createContext("/topology-rids", exchange ->
                HttpSupport.writeJson(exchange, json, topologyRids()));
            server.createContext("/registry/members/wait", exchange -> {
                Contracts.MemberEndpointWaitRequest request =
                    HttpSupport.readJson(exchange, json, Contracts.MemberEndpointWaitRequest.class);
                Wait.until(Duration.ofMillis(Math.max(1, request.timeoutMilliseconds())),
                    "timed out waiting for member endpoint " + request.endpoint(),
                    () -> await(query.memberPeers(Contracts.CHANNEL)).stream()
                        .anyMatch(peer -> request.endpoint().equals(peer.endpoint())) ? Boolean.TRUE : null);
                HttpSupport.writeJson(exchange, json, java.util.Map.of("status", "ready"));
            });
            server.start();
            running = true;
        } catch (Exception error) {
            throw new IllegalStateException("failed to start registry endpoints " + endpoint, error);
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

    private record MemberPeerDto(
        String channelName,
        String serviceRole,
        String endpoint,
        String routingId,
        int weight) {
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
