package systems.zlink.e2e.discoveryregistryha;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.util.HashSet;
import java.util.Set;
import systems.zlink.framework.channels.ZLinkClient;

public final class ClientScenario {
    private final ZLinkClient client;
    private final ObjectMapper json;
    private final HttpClient http = HttpClient.newHttpClient();

    public ClientScenario(ZLinkClient client, ObjectMapper json) {
        this.client = client;
        this.json = json;
    }

    public void run() {
        String scenario = Env.get("ZLINK_JAVA_E2E_SCENARIO");
        waitForExpectedMembers();
        Set<String> providers = requestUntilProviders(Env.csv("ZLINK_JAVA_E2E_EXPECTED_RIDS"));
        if ("DR-C1".equals(scenario)) {
            expectHttpFailure(Env.get("ZLINK_JAVA_E2E_DEAD_HTTP_ENDPOINT"));
        }
        System.out.println("scenario " + scenario + " passed providers=" + providers);
    }

    private void waitForExpectedMembers() {
        Set<String> expected = new HashSet<>(Env.csv("ZLINK_JAVA_E2E_EXPECTED_RIDS"));
        for (String probe : Env.csv("ZLINK_JAVA_E2E_PROBE_HTTP_ENDPOINTS")) {
            long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(15);
            while (System.nanoTime() < deadline) {
                Set<String> observed = memberRids(probe);
                if (observed.containsAll(expected)) {
                    break;
                }
                sleep(250);
            }
            Set<String> finalObserved = memberRids(probe);
            ensure(finalObserved.containsAll(expected),
                "probe " + probe + " missing expected members " + expected + ": " + finalObserved);
        }
    }

    private Set<String> memberRids(String probe) {
        try {
            JsonNode root = json.readTree(get(probe + "/member-peers"));
            Set<String> rids = new HashSet<>();
            for (JsonNode entry : root) {
                String rid = entry.path("routingId").asText("");
                if (!rid.isBlank()) {
                    rids.add(rid);
                }
            }
            return rids;
        } catch (Exception error) {
            return Set.of();
        }
    }

    private Set<String> requestUntilProviders(java.util.List<String> expected) {
        Set<String> providers = new HashSet<>();
        for (int index = 0; index < 120 && !providers.containsAll(expected); index++) {
            Contracts.WorkReply reply = client.requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.WorkRequest("msg-" + index))
                .timeout(Duration.ofSeconds(3))
                .await(Contracts.WorkReply.class);
            ensure(reply.value().equals("work:msg-" + index), "reply payload mismatch");
            providers.add(reply.providerRid());
        }
        ensure(providers.containsAll(expected),
            "messaging did not reach expected providers " + expected + ": " + providers);
        return providers;
    }

    private void expectHttpFailure(String url) {
        if (url == null || url.isBlank()) {
            return;
        }
        try {
            get(url + "/health");
            throw new IllegalStateException("dead registry probe unexpectedly responded: " + url);
        } catch (IllegalStateException expected) {
            if (expected.getMessage().startsWith("dead registry probe")) {
                throw expected;
            }
        }
    }

    private String get(String url) {
        try {
            HttpResponse<String> response = http.send(
                HttpRequest.newBuilder(URI.create(url))
                    .timeout(Duration.ofSeconds(2))
                    .GET()
                    .build(),
                HttpResponse.BodyHandlers.ofString());
            ensure(response.statusCode() >= 200 && response.statusCode() < 300,
                "GET " + url + " returned " + response.statusCode());
            return response.body();
        } catch (IOException error) {
            throw new IllegalStateException("GET failed: " + url, error);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("GET interrupted: " + url, error);
        }
    }

    private static void sleep(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted", error);
        }
    }

    private static void ensure(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
