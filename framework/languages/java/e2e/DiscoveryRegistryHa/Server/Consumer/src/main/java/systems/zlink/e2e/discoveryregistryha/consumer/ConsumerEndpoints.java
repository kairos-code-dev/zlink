package systems.zlink.e2e.discoveryregistryha.consumer;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.time.Duration;
import java.util.List;
import org.springframework.context.SmartLifecycle;
import systems.zlink.e2e.discoveryregistryha.shared.Contracts;
import systems.zlink.e2e.discoveryregistryha.shared.HttpSupport;
import systems.zlink.e2e.discoveryregistryha.shared.Wait;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

public final class ConsumerEndpoints implements SmartLifecycle {
    private final ConsumerOptions options;
    private final ZLinkClient client;
    private final ZLinkFrameworkLifecycle lifecycle;
    private final ObjectMapper json;
    private HttpServer server;
    private boolean running;

    public ConsumerEndpoints(
        ConsumerOptions options,
        ZLinkClient client,
        ZLinkFrameworkLifecycle lifecycle,
        ObjectMapper json) {
        this.options = options;
        this.client = client;
        this.lifecycle = lifecycle;
        this.json = json;
    }

    @Override
    public void start() {
        try {
            server = HttpSupport.createServer(options.httpEndpoint());
            server.createContext("/health", exchange -> HttpSupport.writeJson(
                exchange,
                json,
                java.util.Map.of("status", "ready", "rid", options.rid())));
            server.createContext("/profile/request", exchange -> {
                Contracts.ProfileReq request =
                    HttpSupport.readJson(exchange, json, Contracts.ProfileReq.class);
                HttpSupport.writeJson(exchange, json, request(request));
            });
            server.createContext("/profile/request/wait", exchange -> {
                Contracts.ProfileReq request =
                    HttpSupport.readJson(exchange, json, Contracts.ProfileReq.class);
                Contracts.ProfileRes reply = Wait.until(
                    Duration.ofSeconds(10),
                    "timed out waiting for profile request routing",
                    () -> {
                        try {
                            return request(request);
                        } catch (Exception error) {
                            return null;
                        }
                    });
                HttpSupport.writeJson(exchange, json, reply);
            });
            server.createContext("/locations/status", exchange -> HttpSupport.writeJson(
                exchange,
                json,
                status()));
            server.createContext("/locations/peers", exchange -> HttpSupport.writeJson(
                exchange,
                json,
                peers()));
            server.createContext("/shutdown", exchange -> {
                HttpSupport.writeJson(exchange, json, java.util.Map.of("status", "stopping"));
                HttpSupport.shutdownAsync();
            });
            server.start();
            running = true;
        } catch (Exception error) {
            throw new IllegalStateException(
                "failed to start consumer endpoints " + options.httpEndpoint(), error);
        }
    }

    private Contracts.ProfileRes request(Contracts.ProfileReq request) {
        return client.requestToChannel(Contracts.CHANNEL, request)
            .timeout(Duration.ofSeconds(3))
            .await(Contracts.ProfileRes.class);
    }

    private List<java.util.Map<String, Object>> peers() {
        return lifecycle.monitoringLocationRuntimeQuery().listPeersAsync(new ZLinkPeerLocationFilter(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                Contracts.CHANNEL,
                ZLinkLocationRole.ROUTER,
                null,
                null))
            .toCompletableFuture()
            .join()
            .stream()
            .map(peer -> java.util.Map.<String, Object>of(
                "nodeRid", peer.nodeRid().toString(),
                "endpoint", peer.endpoint(),
                "ownerId", peer.ownerId(),
                "role", peer.role().name(),
                "meshName", peer.meshName()))
            .toList();
    }

    private java.util.Map<String, Object> status() {
        var status = lifecycle.monitoringLocationRuntimeQuery().getStatusAsync().toCompletableFuture().join();
        return java.util.Map.of(
            "storeHealthy", status.storeHealthy(),
            "watchEnabled", status.watchEnabled(),
            "pollingIntervalMillis", status.pollingInterval().toMillis(),
            "lastRefreshAt", status.lastRefreshAt() == null ? "" : status.lastRefreshAt().toString(),
            "lastError", status.lastError() == null ? "" : status.lastError(),
            "ownerLeaseHealthy", status.ownerLeaseHealthy(),
            "ownerLeaseRenewedAt", status.ownerLeaseRenewedAt() == null ? "" : status.ownerLeaseRenewedAt().toString());
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
