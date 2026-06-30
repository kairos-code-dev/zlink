package systems.zlink.e2e.pubsub.registry.Endpoints;

import com.sun.net.httpserver.HttpServer;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import org.springframework.context.SmartLifecycle;
import systems.zlink.e2e.pubsub.registry.Configuration.RegistryOptions;
import systems.zlink.e2e.pubsub.registry.Infrastructure.EvidenceStore;

public final class OperationalEndpoints implements SmartLifecycle {
    private final RegistryOptions options;
    private final EvidenceStore evidence;
    private HttpServer server;
    private boolean running;

    public OperationalEndpoints(RegistryOptions options, EvidenceStore evidence) {
        this.options = options;
        this.evidence = evidence;
    }

    @Override
    public void start() {
        if (options.httpPort() <= 0) {
            return;
        }
        try {
            server = HttpServer.create(new InetSocketAddress("127.0.0.1", options.httpPort()), 0);
            server.createContext("/health", exchange -> {
                byte[] body = "ok\n".getBytes(StandardCharsets.UTF_8);
                exchange.sendResponseHeaders(200, body.length);
                exchange.getResponseBody().write(body);
                exchange.close();
            });
            server.createContext("/evidence", exchange -> {
                byte[] body = evidence.snapshot().getBytes(StandardCharsets.UTF_8);
                exchange.sendResponseHeaders(200, body.length);
                exchange.getResponseBody().write(body);
                exchange.close();
            });
            server.start();
            running = true;
        } catch (Exception error) {
            throw new IllegalStateException("failed to start registry endpoint", error);
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
