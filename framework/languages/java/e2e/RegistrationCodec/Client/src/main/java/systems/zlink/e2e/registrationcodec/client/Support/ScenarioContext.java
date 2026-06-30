package systems.zlink.e2e.registrationcodec.client.Support;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.http.HttpClient;
import systems.zlink.framework.channels.ZLinkClient;

public record ScenarioContext(
    ZLinkClient client,
    Evidence evidence) {
    public static Evidence evidenceFromEnv() {
        return new Evidence(ClientOptions.fromEnv(), new ObjectMapper(), HttpClient.newHttpClient());
    }
}
