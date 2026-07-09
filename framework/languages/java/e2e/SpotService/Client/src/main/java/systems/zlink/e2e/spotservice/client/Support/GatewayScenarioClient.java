package systems.zlink.e2e.spotservice.client.Support;

import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;

public final class GatewayScenarioClient {
    private final String endpoint;
    private final HttpClient http = HttpClient.newHttpClient();

    public GatewayScenarioClient(String endpoint) {
        this.endpoint = endpoint;
    }

    public void runMode(String mode) {
        HttpRequest request = HttpRequest.newBuilder(URI.create(endpoint + "/scenario/" + mode))
            .timeout(Duration.ofMinutes(2))
            .POST(HttpRequest.BodyPublishers.noBody())
            .build();
        try {
            HttpResponse<String> response = http.send(request, HttpResponse.BodyHandlers.ofString());
            if (response.statusCode() < 200 || response.statusCode() >= 300) {
                throw new IllegalStateException(
                    "gateway scenario " + mode + " returned " + response.statusCode() + ": " + response.body());
            }
            System.out.print(response.body());
        } catch (IOException error) {
            throw new IllegalStateException("failed to call gateway scenario " + mode, error);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted while calling gateway scenario " + mode, error);
        }
    }
}
