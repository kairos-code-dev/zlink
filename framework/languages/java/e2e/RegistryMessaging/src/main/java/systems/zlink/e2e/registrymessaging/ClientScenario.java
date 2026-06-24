package systems.zlink.e2e.registrymessaging;

import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CompletionException;
import java.util.concurrent.TimeUnit;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkRouteClient;

public final class ClientScenario {
    private final ZLinkClient client;
    private final ZLinkRouteClient routes;

    public ClientScenario(ZLinkClient client, ZLinkRouteClient routes) {
        this.client = client;
        this.routes = routes;
    }

    public void run() {
        switch (Env.get("ZLINK_JAVA_E2E_SCENARIO", "common")) {
            case "common" -> runCommon();
            case "scale-out" -> runScaleOut();
            case "scale-in" -> runScaleIn();
            case "failover" -> runFailover();
            case "weighted" -> runWeightedDistribution();
            default -> throw new IllegalArgumentException(
                "unknown scenario " + Env.get("ZLINK_JAVA_E2E_SCENARIO"));
        }
    }

    private void runCommon() {
        runRegistryClientServer();
        runManualClientServer();
        runCrossChannelDiscovery();
        runMessageSizeVariation();
        runBackpressureObservation();
        runRouteMesh();
    }

    private void runRegistryClientServer() {
        Set<String> providers = new HashSet<>();
        for (int index = 0; index < 40 && providers.size() < 2; index++) {
            Contracts.ProfileReply reply = request(
                Contracts.API_CHANNEL,
                new Contracts.ProfileRequest("auto-" + index),
                Duration.ofSeconds(2));
            ensure(reply.value().startsWith("profile:auto-"), "RM-A1 reply payload mismatch");
            providers.add(reply.providerRid());
        }
        ensure(!providers.isEmpty(), "RM-A1 did not reach any provider");
        System.out.println("scenario RM-A1 passed");

        Contracts.ProfileReply c1 = request(
            Contracts.API_CHANNEL,
            new Contracts.ProfileRequest("c1"),
            Duration.ofSeconds(2));
        ensure("profile:c1".equals(c1.value()), "RM-C1 reply mismatch");
        send(Contracts.API_CHANNEL, new Contracts.ProfileCommand("cmd-c1"), null);
        System.out.println("scenario RM-C1 passed");

        expectFailure(() -> request(
            Contracts.API_CHANNEL,
            new Contracts.ProfileRequest("slow"),
            Duration.ofMillis(100)));
        Contracts.ProfileReply after = request(
            Contracts.API_CHANNEL,
            new Contracts.ProfileRequest("after"),
            Duration.ofSeconds(2));
        ensure("profile:after".equals(after.value()), "RM-C4 post-timeout request failed");
        sleep(1100);
        Contracts.ProfileReply later = request(
            Contracts.API_CHANNEL,
            new Contracts.ProfileRequest("later"),
            Duration.ofSeconds(2));
        ensure("profile:later".equals(later.value()), "RM-C4 later request failed");
        System.out.println("scenario RM-C4 passed");

        expectFailure(() -> requestWithPacket(
            Contracts.API_CHANNEL,
            new Contracts.ProfileRequest("missing"),
            "MissingProfileRequest",
            Duration.ofSeconds(2)));
        send(Contracts.API_CHANNEL, new Contracts.ProfileCommand("missing-send"), "MissingProfileCommand");
        Contracts.ProfileReply normal = request(
            Contracts.API_CHANNEL,
            new Contracts.ProfileRequest("normal"),
            Duration.ofSeconds(2));
        ensure("profile:normal".equals(normal.value()), "RM-C5 normal request failed");
        System.out.println("scenario RM-C5 passed");
    }

    private void runManualClientServer() {
        Contracts.ProfileReply manual = request(
            "registry.messaging.api.manual",
            new Contracts.ProfileRequest("manual"),
            Duration.ofSeconds(2));
        ensure("api-a".equals(manual.providerRid()), "RM-A2 wrong provider");
        System.out.println("scenario RM-A2 passed");

        Map<String, Integer> counts = new HashMap<>();
        for (int index = 0; index < 80; index++) {
            Contracts.ProfileReply reply = request(
                "registry.messaging.api.manual.multi",
                new Contracts.ProfileRequest("multi-" + index),
                Duration.ofSeconds(2));
            counts.merge(reply.providerRid(), 1, Integer::sum);
        }
        ensure(counts.getOrDefault("api-a", 0) > 0, "RM-C3 did not reach api-a");
        ensure(counts.getOrDefault("api-b", 0) > 0, "RM-C3 did not reach api-b");
        ensure(counts.values().stream().mapToInt(Integer::intValue).sum() == 80, "RM-C3 count mismatch");
        System.out.println("scenario RM-C3 passed");
    }

    private void runWeightedDistribution() {
        Map<String, Integer> counts = new HashMap<>();
        for (int index = 0; index < 300; index++) {
            Contracts.ProfileReply reply = request(
                "registry.messaging.api.manual.multi",
                new Contracts.ProfileRequest("weighted-" + index),
                Duration.ofSeconds(2));
            counts.merge(reply.providerRid(), 1, Integer::sum);
        }
        int high = counts.getOrDefault("api-a", 0);
        int low = counts.getOrDefault("api-b", 0);
        ensure(high > 0 && low > 0, "RM-C7 did not reach both providers: " + counts);
        ensure(high > low, "RM-C7 high weight provider was not preferred: " + counts);
        ensure(high + low == 300, "RM-C7 count mismatch: " + counts);
        System.out.println("scenario RM-C7 passed");
    }

    private void runCrossChannelDiscovery() {
        Contracts.ProfileReply api = request(
            Contracts.API_CHANNEL,
            new Contracts.ProfileRequest("a6-api"),
            Duration.ofSeconds(2));
        ensure(api.providerRid().startsWith("api-"), "RM-A6 api resolved wrong provider");

        Contracts.ProfileReply workflow = request(
            Contracts.WORKFLOW_CHANNEL,
            new Contracts.ProfileRequest("a6-workflow"),
            Duration.ofSeconds(2));
        ensure("workflow-a".equals(workflow.providerRid()), "RM-A6 workflow resolved wrong provider");
        System.out.println("scenario RM-A6 passed");
    }

    private void runMessageSizeVariation() {
        int[] sizes = {8, 64 * 1024, 128 * 1024};
        for (int size : sizes) {
            String payload = "x".repeat(size);
            Contracts.ProfileReply reply = request(
                Contracts.API_CHANNEL,
                new Contracts.ProfileRequest(payload),
                Duration.ofSeconds(5));
            ensure(reply.value().equals("profile:" + payload), "RM-C8 payload mismatch for " + size);
        }
        System.out.println("scenario RM-C8 roundtrip passed");
    }

    private void runBackpressureObservation() {
        java.util.List<java.util.concurrent.CompletionStage<Contracts.ProfileReply>> requests =
            new java.util.ArrayList<>();
        for (int index = 0; index < 40; index++) {
            requests.add(client.requestToChannel(
                    Contracts.API_CHANNEL,
                    new Contracts.ProfileRequest("slow-c9-" + index))
                .timeout(Duration.ofMillis(150))
                .submit(Contracts.ProfileReply.class));
        }

        int failed = 0;
        for (var request : requests) {
            try {
                request.toCompletableFuture().get(2, TimeUnit.SECONDS);
            } catch (Exception expected) {
                failed++;
            }
        }
        ensure(failed > 0, "RM-C9 did not observe bounded backpressure/timeout");
        sleep(5000);

        Contracts.ProfileReply recovered = request(
            Contracts.WORKFLOW_CHANNEL,
            new Contracts.ProfileRequest("c9-recovered"),
            Duration.ofSeconds(5));
        ensure("profile:c9-recovered".equals(recovered.value()),
            "RM-C9 connection did not recover after pressure");
        System.out.println("scenario RM-C9 passed");
    }

    private void runRouteMesh() {
        Contracts.RoutePong toB = routes.requestTo(
                Contracts.ROUTE_CHANNEL,
                RoutingId.from("api-b"),
                new Contracts.RoutePing("target-b"))
            .packetName(Contracts.ROUTE_PACKET)
            .timeout(Duration.ofSeconds(2))
            .await(Contracts.RoutePong.class);
        ensure("api-b".equals(toB.targetRid()), "RM-C2 target rid mismatch");

        expectFailure(() -> routes.requestTo(
                Contracts.ROUTE_CHANNEL,
                RoutingId.from("api-missing"),
                new Contracts.RoutePing("missing"))
            .packetName(Contracts.ROUTE_PACKET)
            .timeout(Duration.ofMillis(500))
            .await(Contracts.RoutePong.class));
        System.out.println("scenario RM-C2 passed");
    }

    private void runScaleOut() {
        for (int index = 0; index < 5; index++) {
            Contracts.ProfileReply reply = request(
                Contracts.API_CHANNEL,
                new Contracts.ProfileRequest("scale-out-before-" + index),
                Duration.ofSeconds(2));
            ensure("api-a".equals(reply.providerRid()), "RM-B1 initial traffic should only use api-a");
        }

        touch(Env.get("ZLINK_JAVA_E2E_READY_FILE"));
        waitForFile(Env.get("ZLINK_JAVA_E2E_CONTINUE_FILE"));

        Set<String> providers = new HashSet<>();
        for (int index = 0; index < 100 && providers.size() < 2; index++) {
            Contracts.ProfileReply reply = request(
                Contracts.API_CHANNEL,
                new Contracts.ProfileRequest("scale-out-after-" + index),
                Duration.ofSeconds(2));
            providers.add(reply.providerRid());
        }
        ensure(providers.contains("api-a") && providers.contains("api-b"),
            "RM-B1 did not route to both providers after scale-out");
        System.out.println("scenario RM-B1 passed");
    }

    private void runScaleIn() {
        Set<String> before = new HashSet<>();
        for (int index = 0; index < 100 && before.size() < 2; index++) {
            Contracts.ProfileReply reply = request(
                Contracts.API_CHANNEL,
                new Contracts.ProfileRequest("scale-in-before-" + index),
                Duration.ofSeconds(2));
            before.add(reply.providerRid());
        }
        ensure(before.contains("api-a") && before.contains("api-b"), "RM-B2 did not start with both providers");

        touch(Env.get("ZLINK_JAVA_E2E_READY_FILE"));
        waitForFile(Env.get("ZLINK_JAVA_E2E_CONTINUE_FILE"));

        for (int index = 0; index < 20; index++) {
            Contracts.ProfileReply reply = request(
                Contracts.API_CHANNEL,
                new Contracts.ProfileRequest("scale-in-after-" + index),
                Duration.ofSeconds(2));
            ensure("api-a".equals(reply.providerRid()), "RM-B2 routed to removed provider");
        }
        System.out.println("scenario RM-B2 passed");
    }

    private void runFailover() {
        Contracts.ProfileReply first = request(
            Contracts.API_CHANNEL,
            new Contracts.ProfileRequest("failover-before"),
            Duration.ofSeconds(2));
        ensure("api-a".equals(first.providerRid()) && "api-a-v1".equals(first.instanceId()),
            "RM-A4 initial provider mismatch");

        touch(Env.get("ZLINK_JAVA_E2E_READY_FILE"));
        waitForFile(Env.get("ZLINK_JAVA_E2E_CONTINUE_FILE"));

        for (int index = 0; index < 20; index++) {
            Contracts.ProfileReply reply = request(
                Contracts.API_CHANNEL,
                new Contracts.ProfileRequest("failover-after-" + index),
                Duration.ofSeconds(2));
            ensure("api-a".equals(reply.providerRid()) && "api-a-v2".equals(reply.instanceId()),
                "RM-A4 did not switch to replacement provider");
        }
        System.out.println("scenario RM-A4 passed");
    }

    private Contracts.ProfileReply request(
        String channelName,
        Contracts.ProfileRequest request,
        Duration timeout) {
        return requestWithPacket(channelName, request, null, timeout);
    }

    private Contracts.ProfileReply requestWithPacket(
        String channelName,
        Contracts.ProfileRequest request,
        String packetName,
        Duration timeout) {
        var call = client.requestToChannel(channelName, request).timeout(timeout);
        if (packetName != null) {
            call = call.packetName(packetName);
        }
        return call.await(Contracts.ProfileReply.class);
    }

    private void send(
        String channelName,
        Contracts.ProfileCommand command,
        String packetName) {
        var call = client.sendToChannel(channelName, command);
        if (packetName != null) {
            call = call.packetName(packetName);
        }
        call.await();
    }

    private static void expectFailure(Runnable action) {
        try {
            action.run();
        } catch (RuntimeException error) {
            return;
        }
        throw new IllegalStateException("operation unexpectedly succeeded");
    }

    private static void touch(String path) {
        if (path.isBlank()) {
            return;
        }
        try {
            Files.writeString(Path.of(path), "ready\n");
        } catch (java.io.IOException error) {
            throw new IllegalStateException("failed to write " + path, error);
        }
    }

    private static void waitForFile(String path) {
        if (path.isBlank()) {
            return;
        }
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20);
        while (System.nanoTime() < deadline) {
            if (Files.exists(Path.of(path))) {
                return;
            }
            sleep(50);
        }
        throw new IllegalStateException("timed out waiting for " + path);
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
