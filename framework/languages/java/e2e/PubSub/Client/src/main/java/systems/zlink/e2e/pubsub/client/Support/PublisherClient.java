package systems.zlink.e2e.pubsub.client.Support;

import java.net.URI;
import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import systems.zlink.e2e.pubsub.shared.Contracts;

public final class PublisherClient {
    private final HttpClient http;
    private final String endpoint;

    public PublisherClient(HttpClient http, String endpoint) {
        this.http = http;
        this.endpoint = endpoint;
    }

    public void publish(String topic, Contracts.EventMsg message) {
        post("/publish/event", topic, message);
    }

    public void publishMissing(String topic, Contracts.EventMsg message) {
        post("/publish/missing", topic, message);
    }

    public boolean canReachHealth() {
        try {
            HttpRequest request = HttpRequest.newBuilder(URI.create(endpoint + "/health"))
                .GET()
                .build();
            HttpResponse<String> response = http.send(request, HttpResponse.BodyHandlers.ofString());
            return response.statusCode() >= 200 && response.statusCode() < 300;
        } catch (Exception ignored) {
            return false;
        }
    }

    private void post(String path, String topic, Contracts.EventMsg message) {
        String uri = endpoint + path
            + "?topic=" + encode(topic)
            + "&scenario=" + encode(message.scenario())
            + "&sequence=" + message.sequence()
            + "&value=" + encode(message.value());
        try {
            HttpRequest request = HttpRequest.newBuilder(URI.create(uri))
                .POST(HttpRequest.BodyPublishers.noBody())
                .build();
            HttpResponse<String> response = http.send(request, HttpResponse.BodyHandlers.ofString());
            if (response.statusCode() < 200 || response.statusCode() >= 300) {
                throw new IllegalStateException("publisher returned " + response.statusCode()
                    + " for " + path + ": " + response.body());
            }
        } catch (Exception error) {
            throw new IllegalStateException("failed to publish through " + endpoint + path, error);
        }
    }

    private static String encode(String value) {
        return URLEncoder.encode(value, StandardCharsets.UTF_8);
    }
}
