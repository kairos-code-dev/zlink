package systems.zlink.e2e.yielddispatch.shared;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import org.springframework.context.SmartLifecycle;

public final class EvidenceHttpServer implements SmartLifecycle {
    private final EvidenceStore evidence;
    private final ObjectMapper json;
    private final String endpoint;
    private HttpServer server;
    private boolean running;

    public EvidenceHttpServer(EvidenceStore evidence, ObjectMapper json, String endpoint) {
        this.evidence = evidence;
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
            server.createContext("/evidence", exchange -> write(
                exchange,
                json.writeValueAsString(evidence.snapshot())));
            server.start();
            running = true;
        } catch (Exception error) {
            throw new IllegalStateException("failed to start evidence endpoint " + endpoint, error);
        }
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
