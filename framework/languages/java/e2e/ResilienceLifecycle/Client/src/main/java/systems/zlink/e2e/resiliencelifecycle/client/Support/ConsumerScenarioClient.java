package systems.zlink.e2e.resiliencelifecycle.client.Support;

import java.time.Duration;
import systems.zlink.httpclient.RawHttpResponse;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class ConsumerScenarioClient {
    private final String endpoint;

    public ConsumerScenarioClient(String endpoint) {
        this.endpoint = endpoint;
    }

    public void runMode(String mode) {
        try (ZLinkHttpClient http = ZLinkHttpClient.create(endpoint).build()) {
            RawHttpResponse response = http.post("/scenario/" + mode)
                .timeout(Duration.ofMinutes(10))
                .submitRaw()
                .toCompletableFuture()
                .join();
            if (response.status() < 200 || response.status() >= 300) {
                throw new IllegalStateException(
                    "consumer scenario " + mode + " returned " + response.status()
                        + ": " + response.body());
            }
        }
    }
}
