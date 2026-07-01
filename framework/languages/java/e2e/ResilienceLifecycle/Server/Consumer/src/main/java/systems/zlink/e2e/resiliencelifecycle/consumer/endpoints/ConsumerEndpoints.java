package systems.zlink.e2e.resiliencelifecycle.consumer.endpoints;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.concurrent.Executors;
import org.springframework.context.SmartLifecycle;
import systems.zlink.e2e.resiliencelifecycle.consumer.scenarios.ConsumerScenario;

public final class ConsumerEndpoints implements SmartLifecycle {
    private final ObjectMapper json;
    private final ConsumerScenario scenario;
    private final String endpoint;
    private HttpServer server;
    private boolean running;

    public ConsumerEndpoints(ObjectMapper json, ConsumerScenario scenario, String endpoint) {
        this.json = json;
        this.scenario = scenario;
        this.endpoint = endpoint;
    }

    @Override
    public void start() {
        try {
            URI uri = URI.create(endpoint);
            server = HttpServer.create(new InetSocketAddress(uri.getHost(), uri.getPort()), 0);
            server.setExecutor(Executors.newCachedThreadPool());
            server.createContext("/health", exchange -> writeJson(exchange, Map.of("status", "ready")));
            server.createContext("/scenario", exchange -> {
                String mode = exchange.getRequestURI().getPath().replaceFirst("^/scenario/?", "");
                if (mode.isBlank()) {
                    mode = "default";
                }
                scenario.run(mode);
                writeJson(exchange, Map.of("status", "passed", "mode", mode));
            });
            server.start();
            running = true;
        } catch (Exception error) {
            throw new IllegalStateException("failed to start consumer endpoint " + endpoint, error);
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

    private void writeJson(com.sun.net.httpserver.HttpExchange exchange, Object value) throws java.io.IOException {
        try {
            byte[] body = json.writeValueAsBytes(value);
            exchange.getResponseHeaders().add("Content-Type", "application/json");
            exchange.sendResponseHeaders(200, body.length);
            exchange.getResponseBody().write(body);
        } catch (RuntimeException error) {
            byte[] body = error.toString().getBytes(StandardCharsets.UTF_8);
            exchange.sendResponseHeaders(500, body.length);
            exchange.getResponseBody().write(body);
        } finally {
            exchange.close();
        }
    }
}
