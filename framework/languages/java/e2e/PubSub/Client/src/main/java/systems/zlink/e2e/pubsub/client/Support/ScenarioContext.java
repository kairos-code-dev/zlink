package systems.zlink.e2e.pubsub.client.Support;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.http.HttpClient;

public record ScenarioContext(
    ClientOptions options,
    PublisherClient publisher,
    Evidence evidence) {
    public static ScenarioContext fromEnv() {
        HttpClient http = HttpClient.newHttpClient();
        ClientOptions options = ClientOptions.fromEnv();
        return new ScenarioContext(
            options,
            new PublisherClient(http, options.publisherHttp()),
            new Evidence(options, new ObjectMapper(), http));
    }
}
