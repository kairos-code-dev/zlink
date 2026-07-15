package systems.zlink.e2e.spotservice.client.Support;

import java.time.Duration;
import systems.zlink.httpclient.RawHttpResponse;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class GatewayScenarioClient {
    private final String endpoint;

    public GatewayScenarioClient(String endpoint) {
        this.endpoint = endpoint;
    }

    public void runMode(String mode) {
        try (ZLinkHttpClient http = ZLinkHttpClient.create(endpoint).build()) {
            RawHttpResponse response = http.post("/scenario/" + mode)
                .timeout(Duration.ofMinutes(2))
                .submitRaw()
                .toCompletableFuture()
                .join();
            if (response.status() < 200 || response.status() >= 300) {
                throw new IllegalStateException(
                    "gateway scenario " + mode + " returned " + response.status()
                        + ": " + response.body());
            }
            System.out.print(response.body());
        }
    }
}
