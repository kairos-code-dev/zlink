package systems.zlink.e2e.discoveryregistryha.consumer;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.time.Duration;
import org.springframework.context.SmartLifecycle;
import systems.zlink.e2e.discoveryregistryha.shared.Contracts;
import systems.zlink.e2e.discoveryregistryha.shared.HttpSupport;
import systems.zlink.e2e.discoveryregistryha.shared.Wait;
import systems.zlink.framework.channels.ZLinkClient;

public final class ConsumerEndpoints implements SmartLifecycle {
    private final ConsumerOptions options;
    private final ZLinkClient client;
    private final ObjectMapper json;
    private HttpServer server;
    private boolean running;

    public ConsumerEndpoints(
        ConsumerOptions options,
        ZLinkClient client,
        ObjectMapper json) {
        this.options = options;
        this.client = client;
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
                Contracts.ProfileRequest request =
                    HttpSupport.readJson(exchange, json, Contracts.ProfileRequest.class);
                HttpSupport.writeJson(exchange, json, request(request));
            });
            server.createContext("/profile/request/wait", exchange -> {
                Contracts.ProfileRequest request =
                    HttpSupport.readJson(exchange, json, Contracts.ProfileRequest.class);
                Contracts.ProfileReply reply = Wait.until(
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

    private Contracts.ProfileReply request(Contracts.ProfileRequest request) {
        return client.requestToChannel(Contracts.CHANNEL, request)
            .timeout(Duration.ofSeconds(3))
            .await(Contracts.ProfileReply.class);
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
