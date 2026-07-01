package systems.zlink.e2e.spotservice.client;

import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import systems.zlink.e2e.spotservice.shared.Env;

public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        String mode = Env.get("ZLINK_JAVA_E2E_CLIENT_MODE", "state1");
        String endpoint = Env.get("ZLINK_JAVA_E2E_GATEWAY_HTTP_ENDPOINT");
        HttpRequest request = HttpRequest.newBuilder(URI.create(endpoint + "/scenario/" + mode))
            .timeout(Duration.ofMinutes(2))
            .POST(HttpRequest.BodyPublishers.noBody())
            .build();
        try {
            HttpResponse<String> response = HttpClient.newHttpClient()
                .send(request, HttpResponse.BodyHandlers.ofString());
            if (response.statusCode() < 200 || response.statusCode() >= 300) {
                throw new IllegalStateException(
                    "gateway scenario returned " + response.statusCode() + ": " + response.body());
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
