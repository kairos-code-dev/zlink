package systems.zlink.e2e.resiliencelifecycle.client.Support;

import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;

public final class ConsumerScenarioClient {
    private final String endpoint;
    private final HttpClient http;

    public ConsumerScenarioClient(String endpoint) {
        this.endpoint = endpoint;
        this.http = HttpClient.newHttpClient();
    }

    public void runMode(String mode) {
        HttpRequest request = HttpRequest.newBuilder(URI.create(endpoint + "/scenario/" + mode))
            .timeout(Duration.ofMinutes(10))
            .POST(HttpRequest.BodyPublishers.noBody())
            .build();
        try {
            HttpResponse<String> response = http.send(request, HttpResponse.BodyHandlers.ofString());
            if (response.statusCode() < 200 || response.statusCode() >= 300) {
                throw new IllegalStateException(
                    "consumer scenario " + mode + " returned " + response.statusCode() + ": " + response.body());
            }
        } catch (IOException error) {
            throw new IllegalStateException("failed to call consumer scenario " + mode, error);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted while calling consumer scenario " + mode, error);
        }
    }

    public static void unsupported(String scenario, String reason) {
        throw new IllegalStateException(scenario + " is a documented Java gap: " + reason);
    }
}
