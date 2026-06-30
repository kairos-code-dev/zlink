package systems.zlink.e2e.pubsub.client.Support;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import systems.zlink.e2e.pubsub.shared.Contracts;

public final class Evidence {
    private final ClientOptions options;
    private final ObjectMapper json;
    private final HttpClient http;

    public Evidence(ClientOptions options, ObjectMapper json, HttpClient http) {
        this.options = options;
        this.json = json;
        this.http = http;
    }

    public Contracts.EvidenceSnapshot snapshot(String subscriberRid) {
        String endpoint = options.subscriberHttp(subscriberRid);
        try {
            HttpRequest request = HttpRequest.newBuilder(URI.create(endpoint + "/evidence"))
                .GET()
                .build();
            HttpResponse<String> response = http.send(request, HttpResponse.BodyHandlers.ofString());
            return json.readValue(response.body(), Contracts.EvidenceSnapshot.class);
        } catch (Exception error) {
            throw new IllegalStateException("failed to fetch evidence from " + subscriberRid, error);
        }
    }
}
