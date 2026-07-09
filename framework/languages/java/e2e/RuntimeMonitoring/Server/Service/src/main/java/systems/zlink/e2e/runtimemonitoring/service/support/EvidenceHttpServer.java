package systems.zlink.e2e.runtimemonitoring.service.support;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.SmartLifecycle;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions;

public final class EvidenceHttpServer implements SmartLifecycle {
    private final EvidenceState state;
    private final ObjectMapper json;
    private final ZLinkChannelRuntimeOptions runtimeOptions;
    private final ConfigurableApplicationContext applicationContext;
    private final String endpoint;
    private HttpServer server;
    private boolean running;

    public EvidenceHttpServer(
        EvidenceState state,
        ObjectMapper json,
        ZLinkChannelRuntimeOptions runtimeOptions,
        ConfigurableApplicationContext applicationContext,
        String endpoint) {
        this.state = state;
        this.json = json;
        this.runtimeOptions = runtimeOptions;
        this.applicationContext = applicationContext;
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
                json.writeValueAsString(state.snapshot())));
            server.createContext("/admin/drain", exchange -> {
                setWeight(0, "drain");
                write(exchange, json.writeValueAsString(new AdminResult("drained", 0)));
            });
            server.createContext("/admin/restore", exchange -> {
                setWeight(100, "restore");
                write(exchange, json.writeValueAsString(new AdminResult("restored", 100)));
            });
            server.createContext("/shutdown", exchange -> {
                write(exchange, json.writeValueAsString(new AdminResult("stopping", -1)));
                Thread shutdown = new Thread(applicationContext::close, "runtime-monitoring-shutdown");
                shutdown.setDaemon(false);
                shutdown.start();
            });
            server.start();
            running = true;
        } catch (Exception error) {
            throw new IllegalStateException("failed to start evidence endpoint " + endpoint, error);
        }
    }

    private void setWeight(int weight, String action) {
        runtimeOptions
            .clientServerChannel(Contracts.CHANNEL)
            .configureServerSocket()
            .weight(weight);
        state.record("admin", state.rid(), action, "weight=" + weight);
    }

    private record AdminResult(String status, int weight) {
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
