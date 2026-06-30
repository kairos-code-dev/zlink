package systems.zlink.e2e.pubsub.subscriber.Endpoints;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import org.springframework.context.SmartLifecycle;
import systems.zlink.e2e.pubsub.subscriber.Configuration.SubscriberOptions;
import systems.zlink.e2e.pubsub.subscriber.Infrastructure.EvidenceStore;

public final class OperationalEndpoints implements SmartLifecycle {
    private final SubscriberOptions options;
    private final EvidenceStore evidence;
    private final ObjectMapper json;
    private HttpServer server;
    private boolean running;

    public OperationalEndpoints(
        SubscriberOptions options,
        EvidenceStore evidence,
        ObjectMapper json) {
        this.options = options;
        this.evidence = evidence;
        this.json = json;
    }

    @Override
    public void start() {
        if (options.httpEndpoint() == null || options.httpEndpoint().isBlank()) {
            return;
        }
        try {
            URI uri = URI.create(options.httpEndpoint());
            server = HttpServer.create(new InetSocketAddress(uri.getHost(), uri.getPort()), 0);
            server.createContext("/health", exchange -> {
                byte[] body = "ok\n".getBytes(StandardCharsets.UTF_8);
                exchange.sendResponseHeaders(200, body.length);
                exchange.getResponseBody().write(body);
                exchange.close();
            });
            server.createContext("/evidence", exchange -> {
                byte[] body = json.writeValueAsBytes(evidence.snapshot());
                exchange.getResponseHeaders().add("Content-Type", "application/json");
                exchange.sendResponseHeaders(200, body.length);
                exchange.getResponseBody().write(body);
                exchange.close();
            });
            server.start();
            running = true;
        } catch (Exception error) {
            throw new IllegalStateException("failed to start evidence endpoint " + options.httpEndpoint(), error);
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
