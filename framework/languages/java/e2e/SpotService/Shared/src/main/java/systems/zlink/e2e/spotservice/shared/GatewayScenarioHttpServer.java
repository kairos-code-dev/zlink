package systems.zlink.e2e.spotservice.shared;

import com.sun.net.httpserver.HttpServer;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.util.UUID;
import org.springframework.context.SmartLifecycle;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotManager;

public final class GatewayScenarioHttpServer implements SmartLifecycle {
    private final String endpoint;
    private final ZLinkSpotManager spots;
    private HttpServer server;
    private boolean running;

    public GatewayScenarioHttpServer(String endpoint, ZLinkSpotManager spots) {
        this.endpoint = endpoint;
        this.spots = spots;
    }

    @Override
    public void start() {
        if (endpoint == null || endpoint.isBlank()) {
            return;
        }
        try {
            URI uri = URI.create(endpoint);
            server = HttpServer.create(new InetSocketAddress(uri.getHost(), uri.getPort()), 0);
            server.createContext("/health", exchange -> write(exchange, 200, "ok\n"));
            server.createContext("/scenario/", exchange -> {
                String mode = exchange.getRequestURI().getPath().substring("/scenario/".length());
                try {
                    ClientDriverSpot.configure(mode);
                    spots.create(ClientDriverSpot.class, RoutingId.from(
                            "gateway-driver-" + mode + "-" + UUID.randomUUID().toString().replace("-", "")))
                        .toCompletableFuture()
                        .join();
                    ClientDriverSpot.awaitResult();
                    write(exchange, 200, "spot-service e2e mode=" + mode + " result=passed\n");
                } catch (Throwable error) {
                    error.printStackTrace(System.err);
                    write(exchange, 500, "spot-service e2e mode=" + mode + " result=failed: " + error + "\n");
                }
            });
            server.start();
            running = true;
        } catch (Exception error) {
            throw new IllegalStateException("failed to start gateway scenario endpoint " + endpoint, error);
        }
    }

    private static void write(
        com.sun.net.httpserver.HttpExchange exchange,
        int status,
        String value) throws java.io.IOException {
        byte[] body = value.getBytes(StandardCharsets.UTF_8);
        exchange.getResponseHeaders().add("Content-Type", "text/plain");
        exchange.sendResponseHeaders(status, body.length);
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
