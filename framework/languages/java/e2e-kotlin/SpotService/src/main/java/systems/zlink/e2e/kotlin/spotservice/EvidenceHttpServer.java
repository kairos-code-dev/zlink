package systems.zlink.e2e.kotlin.spotservice;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import org.springframework.context.SmartLifecycle;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotManager;

public final class EvidenceHttpServer implements SmartLifecycle {
    private final ScenarioState state;
    private final ObjectMapper json;
    private final String endpoint;
    private final ZLinkSpotManager spots;
    private HttpServer server;
    private boolean running;

    public EvidenceHttpServer(
        ScenarioState state,
        ObjectMapper json,
        String endpoint,
        ZLinkSpotManager spots) {
        this.state = state;
        this.json = json;
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
            server.createContext("/health", exchange -> {
                byte[] body = "ok\n".getBytes(StandardCharsets.UTF_8);
                exchange.sendResponseHeaders(200, body.length);
                exchange.getResponseBody().write(body);
                exchange.close();
            });
            server.createContext("/evidence", exchange -> {
                byte[] body = json.writeValueAsBytes(state.snapshot());
                exchange.getResponseHeaders().add("Content-Type", "application/json");
                exchange.sendResponseHeaders(200, body.length);
                exchange.getResponseBody().write(body);
                exchange.close();
            });
            server.createContext("/admin/close", exchange -> {
                String rid = queryValue(exchange.getRequestURI(), "rid");
                if (rid == null || rid.isBlank()) {
                    write(exchange, 400, "missing rid\n");
                    return;
                }
                boolean closed;
                try {
                    closed = spots.close(RoutingId.from(rid))
                        .toCompletableFuture()
                        .get(5, java.util.concurrent.TimeUnit.SECONDS);
                } catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("spot close interrupted", error);
                } catch (java.util.concurrent.ExecutionException
                         | java.util.concurrent.TimeoutException error) {
                    throw new IllegalStateException("spot close failed", error);
                }
                write(exchange, 200, "{\"closed\":" + closed + "}\n");
            });
            server.createContext("/admin/create-timer", exchange -> {
                String rid = queryValue(exchange.getRequestURI(), "rid");
                if (rid == null || rid.isBlank()) {
                    write(exchange, 400, "missing rid\n");
                    return;
                }
                try {
                    spots.getOrCreate(TimerScenarioSpot.class, RoutingId.from(rid), "e2e")
                        .toCompletableFuture()
                        .get(5, java.util.concurrent.TimeUnit.SECONDS);
                } catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("timer spot create interrupted", error);
                } catch (java.util.concurrent.ExecutionException
                         | java.util.concurrent.TimeoutException error) {
                    throw new IllegalStateException("timer spot create failed", error);
                }
                write(exchange, 200, "{\"created\":true}\n");
            });
            server.createContext("/admin/type-mismatch", exchange -> {
                String rid = queryValue(exchange.getRequestURI(), "rid");
                if (rid == null || rid.isBlank()) {
                    write(exchange, 400, "missing rid\n");
                    return;
                }
                try {
                    spots.getOrCreate(MismatchedSpot.class, RoutingId.from(rid))
                        .toCompletableFuture()
                        .get(5, java.util.concurrent.TimeUnit.SECONDS);
                    write(exchange, 500, "{\"mismatch\":false}\n");
                    return;
                } catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("spot type mismatch interrupted", error);
                } catch (java.util.concurrent.ExecutionException error) {
                    state.record("SpotTypeMismatch", rid, error.getCause().getMessage());
                } catch (java.util.concurrent.TimeoutException error) {
                    throw new IllegalStateException("spot type mismatch timed out", error);
                } catch (RuntimeException error) {
                    state.record("SpotTypeMismatch", rid, error.getMessage());
                }
                try {
                    spots.getOrCreate(UserSpot.class, RoutingId.from(rid))
                        .toCompletableFuture()
                        .get(5, java.util.concurrent.TimeUnit.SECONDS);
                    state.record("SpotTypeMismatchStateOk", rid, "existing");
                } catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("spot type mismatch follow-up interrupted", error);
                } catch (java.util.concurrent.ExecutionException
                         | java.util.concurrent.TimeoutException error) {
                    throw new IllegalStateException("spot type mismatch follow-up failed", error);
                }
                write(exchange, 200, "{\"mismatch\":true}\n");
            });
            server.start();
            running = true;
        } catch (Exception error) {
            throw new IllegalStateException("failed to start evidence endpoint " + endpoint, error);
        }
    }

    private static String queryValue(URI uri, String name) {
        String query = uri.getRawQuery();
        if (query == null || query.isBlank()) {
            return null;
        }
        for (String part : query.split("&")) {
            String[] pair = part.split("=", 2);
            if (pair.length == 2 && name.equals(pair[0])) {
                return java.net.URLDecoder.decode(pair[1], StandardCharsets.UTF_8);
            }
        }
        return null;
    }

    private static void write(
        com.sun.net.httpserver.HttpExchange exchange,
        int status,
        String value) throws java.io.IOException {
        byte[] body = value.getBytes(StandardCharsets.UTF_8);
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
