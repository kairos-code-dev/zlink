package systems.zlink.e2e.storefailure.consumer;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import org.springframework.context.SmartLifecycle;
import systems.zlink.e2e.storefailure.shared.Contracts;
import systems.zlink.e2e.storefailure.shared.HttpSupport;
import systems.zlink.e2e.storefailure.shared.Wait;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkPageRequest;
import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

public final class ConsumerEndpoints implements SmartLifecycle {
    private final ConsumerOptions options;
    private final ZLinkClient client;
    private final ZLinkFrameworkLifecycle lifecycle;
    private final ZLinkLocationStore locationStore;
    private final LocationStoreDelayState delayState;
    private final ObjectMapper json;
    private HttpServer server;
    private ExecutorService executor;
    private boolean running;

    public ConsumerEndpoints(
        ConsumerOptions options,
        ZLinkClient client,
        ZLinkFrameworkLifecycle lifecycle,
        ZLinkLocationStore locationStore,
        LocationStoreDelayState delayState,
        ObjectMapper json) {
        this.options = options;
        this.client = client;
        this.lifecycle = lifecycle;
        this.locationStore = locationStore;
        this.delayState = delayState;
        this.json = json;
    }

    @Override
    public void start() {
        try {
            server = HttpSupport.createServer(options.httpEndpoint());
            executor = Executors.newFixedThreadPool(8);
            server.setExecutor(executor);
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
            server.createContext("/admin/store-delay", exchange -> {
                Contracts.StoreDelayReq request =
                    HttpSupport.readJson(exchange, json, Contracts.StoreDelayReq.class);
                delayState.setDelay(Duration.ofMillis(request.delayMilliseconds()));
                HttpSupport.writeJson(
                    exchange,
                    json,
                    java.util.Map.of("delayMilliseconds", delayState.delayMilliseconds()));
            });
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
            .submit(Contracts.ProfileRes.class)
            .toCompletableFuture()
            .join();
    }

    private List<java.util.Map<String, Object>> peers() {
        return locationStore.listClientServers(
                Contracts.CHANNEL,
                new ZLinkPageRequest(1_000, null))
            .toCompletableFuture()
            .join()
            .items().stream()
            .map(server -> java.util.Map.<String, Object>of(
                "nodeRid", server.serverRid().toString(),
                "endpoint", server.endpoint(),
                "ownerId", server.ownerId(),
                "role", "ROUTER",
                "meshName", server.channelName()))
            .toList();
    }

    private java.util.Map<String, Object> status() {
        var status = lifecycle.monitoringLocationRuntimeQuery().getStatus().toCompletableFuture().join();
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
        if (executor != null) {
            executor.shutdownNow();
            executor = null;
        }
        running = false;
    }

    @Override
    public boolean isRunning() {
        return running;
    }
}
