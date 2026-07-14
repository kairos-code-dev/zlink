package systems.zlink.e2e.runtimemonitoring.client.Support;

import java.time.Duration;
import systems.zlink.httpclient.RawHttpResponse;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class TriggerScenarioClient implements AutoCloseable {
    private final ZLinkHttpClient http;

    public TriggerScenarioClient(String endpoint) {
        this.http = ZLinkHttpClient.create(endpoint).build();
    }

    public void runScenario(String name) {
        RawHttpResponse response;
        try {
            response = http.post("/scenario/" + name)
                .timeout(Duration.ofMinutes(5))
                .submitRaw()
                .toCompletableFuture()
                .join();
        } catch (RuntimeException error) {
            throw new IllegalStateException("failed to call trigger scenario " + name, error);
        }
        if (response.status() < 200 || response.status() >= 300) {
            throw new IllegalStateException(
                "trigger scenario " + name + " returned " + response.status() + ": " + response.body());
        }
        System.out.print(response.body());
    }

    @Override
    public void close() {
        http.close();
    }
}
