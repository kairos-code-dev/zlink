package systems.zlink.e2e.resiliencelifecycle;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeUnit;
import systems.zlink.contracts.service.registry.ServiceRole;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.registry.ZLinkRegistryQueryClient;

public final class ClientScenario {
    private final ZLinkClient client;
    private final ZLinkRegistryQueryClient registry;
    private final ObjectMapper json;
    private final HttpClient http = HttpClient.newHttpClient();

    public ClientScenario(
        ZLinkClient client,
        ZLinkRegistryQueryClient registry,
        ObjectMapper json) {
        this.client = client;
        this.registry = registry;
        this.json = json;
    }

    public void run(String mode) {
        if ("restart".equals(mode)) {
            runServerRestart();
            return;
        }
        runClientTimeoutCleanup();
        runDrainRestore();
        runDrainInFlight();
        runDispatchErrorMarker();
        runGracefulShutdown();
    }

    private void runServerRestart() {
        waitForTopology(2);
        post(adminB() + "/admin/drain");
        waitForWeight(adminB(), 0);
        collectStableProvidersWithout("a1-before-restart", "api-b", "api-a");
        signal("a1-ready");
        waitForSignal("a1-down");
        expectRestartWindowFailure();
        signal("a1-down-observed");
        waitForSignal("a1-up");
        waitForTopology(2);
        collectStableProvidersWithout("a1-after-restart", "api-b", "api-a");
        post(adminB() + "/admin/restore");
        waitForWeight(adminB(), 100);
        System.out.println("scenario RL-A1 passed");
    }

    private void expectRestartWindowFailure() {
        try {
            client.requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.WorkRequest("a1-down-window"))
                .timeout(Duration.ofMillis(700))
                .await(Contracts.WorkReply.class);
            throw new IllegalStateException("RL-A1 down-window request unexpectedly completed");
        } catch (RuntimeException expected) {
            // The scenario only requires a public failure while the sole admissible provider is down.
        }
    }

    private void runClientTimeoutCleanup() {
        try {
            client.requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.WorkRequest("timeout"))
                .timeout(Duration.ofMillis(300))
                .await(Contracts.WorkReply.class);
            throw new IllegalStateException("RL-B1 timeout request unexpectedly completed");
        } catch (RuntimeException expected) {
            waitForEvidenceAny("TimeoutStarted", adminA(), adminB());
        }
        sleep(1800);
        Contracts.WorkReply followUp = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.WorkRequest("b1-follow-up"))
            .timeout(Duration.ofSeconds(3))
            .await(Contracts.WorkReply.class);
        ensure("work:b1-follow-up".equals(followUp.value()), "RL-B1 follow-up payload mismatch");
        System.out.println("scenario RL-B1 passed");
    }

    private void runDrainRestore() {
        waitForTopology(2);
        Set<String> warm = collectProviders("b4-warm", 80, 2);
        ensure(warm.contains("api-a") && warm.contains("api-b"),
            "RL-B4 warmup did not reach both providers: " + warm);

        post(adminA() + "/admin/drain");
        waitForWeight(adminA(), 0);
        Set<String> drained = collectStableProvidersWithout("b4-drained", "api-a", "api-b");
        ensure(get(adminA() + "/health").contains("ok"), "RL-B4 drained provider health failed");
        waitForTopology(2);

        post(adminA() + "/admin/restore");
        waitForWeight(adminA(), 100);
        Set<String> restored = collectProviders("b4-restored", 120, 2);
        ensure(restored.contains("api-a"), "RL-B4 restored provider did not receive traffic");
        System.out.println("scenario RL-B4 passed");
    }

    private void runDrainInFlight() {
        post(adminB() + "/admin/drain");
        waitForWeight(adminB(), 0);
        sleep(1500);

        CompletionStage<Contracts.WorkReply> slow = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.WorkRequest("slow"))
            .timeout(Duration.ofSeconds(15))
            .submit(Contracts.WorkReply.class);
        waitForEvidence(adminA(), "SlowStarted");

        post(adminA() + "/admin/drain");
        waitForWeight(adminA(), 0);
        post(adminB() + "/admin/restore");
        waitForWeight(adminB(), 100);
        collectStableProvidersWithout("b5-after-drain", "api-a", "api-b");

        post(adminA() + "/admin/release-slow");
        Contracts.WorkReply slowReply;
        try {
            slowReply = slow.toCompletableFuture().get(20, TimeUnit.SECONDS);
        } catch (Exception error) {
            throw new IllegalStateException("RL-B5 slow request did not complete", error);
        }
        ensure("api-a".equals(slowReply.providerRid()),
            "RL-B5 slow request was not served by api-a: " + slowReply.providerRid());
        ensure("work:slow".equals(slowReply.value()), "RL-B5 slow reply payload mismatch");

        post(adminA() + "/admin/restore");
        waitForWeight(adminA(), 100);
        System.out.println("scenario RL-B5 passed");
    }

    private void runDispatchErrorMarker() {
        try {
            client.requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.UnhandledRequest("d3-missing-handler"))
                .timeout(Duration.ofSeconds(3))
                .await(Contracts.WorkReply.class);
            throw new IllegalStateException("RL-D3 missing handler request unexpectedly completed");
        } catch (RuntimeException expected) {
            waitForDispatchErrorAny("UnhandledRequest", adminA(), adminB());
        }
        Contracts.WorkReply followUp = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.WorkRequest("d3-follow-up"))
            .timeout(Duration.ofSeconds(3))
            .await(Contracts.WorkReply.class);
        ensure("work:d3-follow-up".equals(followUp.value()), "RL-D3 follow-up payload mismatch");
        System.out.println("scenario RL-D3 passed");
    }

    private void waitForDispatchErrorAny(String packetName, String... baseUrls) {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10);
        while (System.nanoTime() < deadline) {
            for (String baseUrl : baseUrls) {
                try {
                    JsonNode entries = json.readTree(get(baseUrl + "/evidence")).path("entries");
                    if (entries.isArray()) {
                        for (JsonNode entry : entries) {
                            String marker = entry.path("marker").asText();
                            String value = entry.path("value").asText();
                            if ("DispatchError".equals(marker)
                                && value.contains("HANDLER_MISSING")
                                && value.contains("REPLY_ERROR")
                                && value.contains(packetName)) {
                                return;
                            }
                        }
                    }
                } catch (Exception ignored) {
                }
            }
            sleep(100);
        }
        throw new IllegalStateException(
            "dispatch error marker for " + packetName + " was not observed");
    }

    private void runGracefulShutdown() {
        waitForTopology(2);
        Contracts.WorkReply beforeShutdown = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.WorkRequest("b3-before-shutdown"))
            .timeout(Duration.ofSeconds(3))
            .await(Contracts.WorkReply.class);
        ensure("work:b3-before-shutdown".equals(beforeShutdown.value()),
            "RL-B3 pre-shutdown reply payload mismatch");

        post(adminB() + "/admin/shutdown");
        waitForTopology(1);
        Set<String> providers = collectStableProvidersWithout("b3-after-shutdown", "api-b", "api-a");
        ensure(providers.contains("api-a"), "RL-B3 did not converge to api-a after api-b shutdown");
        System.out.println("scenario RL-B3 passed");
    }

    private Set<String> collectProviders(String prefix, int attempts, int expectedCount) {
        Set<String> providers = new HashSet<>();
        for (int index = 0; index < attempts && providers.size() < expectedCount; index++) {
            Contracts.WorkReply reply = client.requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.WorkRequest(prefix + "-" + index))
                .timeout(Duration.ofSeconds(3))
                .await(Contracts.WorkReply.class);
            ensure(reply.value().equals("work:" + prefix + "-" + index),
                "reply payload mismatch for " + prefix + "-" + index);
            providers.add(reply.providerRid());
        }
        return providers;
    }

    private Set<String> collectStableProvidersWithout(
        String prefix,
        String forbidden,
        String required) {
        for (int window = 0; window < 30; window++) {
            Set<String> providers = collectProviders(prefix + "-window-" + window, 5, 1);
            if (!providers.contains(forbidden) && providers.contains(required)) {
                return providers;
            }
            sleep(300);
        }
        throw new IllegalStateException(
            prefix + " did not converge away from " + forbidden + " to " + required);
    }

    private void waitForTopology(int expectedRouters) {
        if (registry == null) {
            return;
        }
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10);
        while (System.nanoTime() < deadline) {
            try {
                long count = registry.topology().toCompletableFuture()
                    .get(3, TimeUnit.SECONDS)
                    .stream()
                    .filter(entry -> Contracts.CHANNEL.equals(entry.channelName()))
                    .filter(entry -> entry.serviceRole() == ServiceRole.ROUTER)
                    .count();
                if (count >= expectedRouters) {
                    return;
                }
            } catch (Exception ignored) {
            }
            sleep(200);
        }
        throw new IllegalStateException("registry topology did not report " + expectedRouters
            + " routers for " + Contracts.CHANNEL);
    }

    private void waitForWeight(String baseUrl, int expected) {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (System.nanoTime() < deadline) {
            try {
                JsonNode node = json.readTree(get(baseUrl + "/admin/weight"));
                if (node.path("weight").asInt(-1) == expected) {
                    return;
                }
            } catch (Exception ignored) {
            }
            sleep(100);
        }
        throw new IllegalStateException("weight did not become " + expected + " for " + baseUrl);
    }

    private void waitForEvidence(String baseUrl, String marker) {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10);
        while (System.nanoTime() < deadline) {
            try {
                JsonNode entries = json.readTree(get(baseUrl + "/evidence")).path("entries");
                if (entries.isArray()) {
                    for (JsonNode entry : entries) {
                        if (marker.equals(entry.path("marker").asText())) {
                            return;
                        }
                    }
                }
            } catch (Exception ignored) {
            }
            sleep(100);
        }
        throw new IllegalStateException("marker " + marker + " was not observed at " + baseUrl);
    }

    private void waitForEvidenceAny(String marker, String... baseUrls) {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10);
        while (System.nanoTime() < deadline) {
            for (String baseUrl : baseUrls) {
                try {
                    JsonNode entries = json.readTree(get(baseUrl + "/evidence")).path("entries");
                    if (entries.isArray()) {
                        for (JsonNode entry : entries) {
                            if (marker.equals(entry.path("marker").asText())) {
                                return;
                            }
                        }
                    }
                } catch (Exception ignored) {
                }
            }
            sleep(100);
        }
        throw new IllegalStateException("marker " + marker + " was not observed at any provider");
    }

    private String adminA() {
        return Env.get("ZLINK_JAVA_E2E_HTTP_A_ENDPOINT");
    }

    private String adminB() {
        return Env.get("ZLINK_JAVA_E2E_HTTP_B_ENDPOINT");
    }

    private void signal(String name) {
        Path dir = controlDir();
        try {
            Files.createDirectories(dir);
            Files.writeString(dir.resolve(name), "ok\n");
        } catch (IOException error) {
            throw new IllegalStateException("failed to write control signal " + name, error);
        }
    }

    private void waitForSignal(String name) {
        Path file = controlDir().resolve(name);
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(30);
        while (System.nanoTime() < deadline) {
            if (Files.exists(file)) {
                return;
            }
            sleep(100);
        }
        throw new IllegalStateException("control signal was not observed: " + name);
    }

    private Path controlDir() {
        String value = Env.get("ZLINK_JAVA_E2E_CONTROL_DIR");
        if (value.isBlank()) {
            throw new IllegalStateException("ZLINK_JAVA_E2E_CONTROL_DIR is required");
        }
        return Path.of(value);
    }

    private String get(String url) {
        try {
            HttpRequest request = HttpRequest.newBuilder(URI.create(url))
                .timeout(Duration.ofSeconds(5))
                .GET()
                .build();
            HttpResponse<String> response = http.send(
                request,
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

    private void post(String url) {
        try {
            HttpRequest request = HttpRequest.newBuilder(URI.create(url))
                .timeout(Duration.ofSeconds(5))
                .POST(HttpRequest.BodyPublishers.noBody())
                .build();
            HttpResponse<String> response = http.send(
                request,
                HttpResponse.BodyHandlers.ofString());
            ensure(response.statusCode() >= 200 && response.statusCode() < 300,
                "POST " + url + " returned " + response.statusCode());
        } catch (IOException error) {
            throw new IllegalStateException("POST failed: " + url, error);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("POST interrupted: " + url, error);
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
