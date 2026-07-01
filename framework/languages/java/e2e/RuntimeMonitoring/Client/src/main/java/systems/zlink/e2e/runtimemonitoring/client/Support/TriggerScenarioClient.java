package systems.zlink.e2e.runtimemonitoring.client.Support;

import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;

public final class TriggerScenarioClient {
    private final String endpoint;
    private final HttpClient http = HttpClient.newHttpClient();

    public TriggerScenarioClient(String endpoint) {
        this.endpoint = endpoint;
    }

    public void runScenario(String name) {
        HttpRequest request = HttpRequest.newBuilder(URI.create(endpoint + "/scenario/" + name))
            .timeout(Duration.ofMinutes(5))
            .POST(HttpRequest.BodyPublishers.noBody())
            .build();
        try {
            HttpResponse<String> response = http.send(request, HttpResponse.BodyHandlers.ofString());
            if (response.statusCode() < 200 || response.statusCode() >= 300) {
                throw new IllegalStateException(
                    "trigger scenario " + name + " returned " + response.statusCode() + ": " + response.body());
            }
            System.out.print(response.body());
        } catch (IOException error) {
            throw new IllegalStateException("failed to call trigger scenario " + name, error);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted while calling trigger scenario " + name, error);
        }
    }
}
