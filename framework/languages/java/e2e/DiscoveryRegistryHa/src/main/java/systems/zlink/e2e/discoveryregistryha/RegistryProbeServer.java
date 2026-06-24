package systems.zlink.e2e.discoveryregistryha;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;
import org.springframework.context.SmartLifecycle;
import systems.zlink.framework.registry.ZLinkRegistryQuery;

public final class RegistryProbeServer implements SmartLifecycle {
    private final ZLinkRegistryQuery query;
    private final ObjectMapper json;
    private final String endpoint;
    private HttpServer server;
    private boolean running;

    public RegistryProbeServer(
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
            URI uri = URI.create(endpoint);
            server = HttpServer.create(new InetSocketAddress(uri.getHost(), uri.getPort()), 0);
            server.createContext("/health", exchange -> write(exchange, "ok\n"));
            server.createContext("/status", exchange -> write(
                exchange,
                json.writeValueAsString(await(query.status()))));
            server.createContext("/member-peers", exchange -> write(
                exchange,
                json.writeValueAsString(await(query.memberPeers(Contracts.CHANNEL)).stream()
                    .map(peer -> new MemberPeerDto(
                        peer.channelName(),
                        peer.serviceRole().name(),
                        peer.endpoint(),
                        peer.routingId().toString(),
                        peer.weight()))
                    .toList())));
            server.createContext("/topology", exchange -> write(
                exchange,
                json.writeValueAsString(await(query.topology()))));
            server.createContext("/topology-rids", exchange -> write(
                exchange,
                json.writeValueAsString(await(query.topology()).stream()
                    .filter(entry -> Contracts.CHANNEL.equals(entry.channelName()))
                    .map(entry -> entry.routingId().toString())
                    .sorted()
                    .toList())));
            server.start();
            running = true;
        } catch (Exception error) {
            throw new IllegalStateException("failed to start registry probe " + endpoint, error);
        }
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

    private static void write(
        com.sun.net.httpserver.HttpExchange exchange,
        String value) throws java.io.IOException {
        byte[] body = value.getBytes(StandardCharsets.UTF_8);
        exchange.getResponseHeaders().add("Content-Type", "application/json");
        exchange.sendResponseHeaders(200, body.length);
        exchange.getResponseBody().write(body);
        exchange.close();
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
