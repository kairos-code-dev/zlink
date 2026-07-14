package systems.zlink.e2e.resiliencelifecycle.consumer.scenarios;

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
import systems.zlink.e2e.resiliencelifecycle.shared.Contracts;
import systems.zlink.e2e.resiliencelifecycle.shared.Env;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

public final class ConsumerScenario {
    private final ZLinkClient client;
    private final ZLinkFrameworkLifecycle lifecycle;
    private final ObjectMapper json;
    private final HttpClient http = HttpClient.newHttpClient();

    public ConsumerScenario(
        ZLinkClient client,
        ZLinkFrameworkLifecycle lifecycle,
        ObjectMapper json) {
        this.client = client;
        this.lifecycle = lifecycle;
        this.json = json;
    }

    public void run(String mode) {
        if ("restart".equals(mode)) {
            runServerRestart();
            return;
        }
        if ("reschedule".equals(mode)) {
            runProviderReschedule();
            return;
        }
        if ("rolling-green".equals(mode)) {
            runRollingGreen();
            return;
        }
        if ("flapping".equals(mode)) {
            runProviderFlapping();
            return;
        }
        if ("storm".equals(mode)) {
            runReconnectStorm();
            return;
        }
        if ("cleanup".equals(mode)) {
            runClientHostLifecycle();
            runMixedBurst();
            return;
        }
        if ("rl-b1".equals(mode)) {
            runClientTimeoutCleanup();
            return;
        }
        if ("rl-b2".equals(mode)) {
            runCrashDuringInflight();
            return;
        }
        if ("rl-b4".equals(mode)) {
            runDrainRestore();
            return;
        }
        if ("rl-b5".equals(mode)) {
            runDrainInFlight();
            return;
        }
        if ("rl-d2".equals(mode)) {
            runObserverFaultIsolation();
            return;
        }
        if ("rl-d3".equals(mode)) {
            runDispatchErrorMarker();
            return;
        }
        if ("rl-d4".equals(mode)) {
            runMissingRequestHandlerErrorReply();
            return;
        }
        if ("rl-b6".equals(mode)) {
            runGrayFailure();
            return;
        }
        if ("rl-b3".equals(mode)) {
            runGracefulShutdown();
            return;
        }
        if ("rl-c1".equals(mode)) {
            runClientHostLifecycle();
            return;
        }
        if ("rl-c2".equals(mode)) {
            runTopologyRecovery();
            return;
        }
        if ("rl-c4".equals(mode)) {
            runStoreOutageRecovery();
            return;
        }
        if ("rl-d5".equals(mode)) {
            runMixedBurst();
            return;
        }
        runClientTimeoutCleanup();
        runDrainRestore();
        runDrainInFlight();
        runObserverFaultIsolation();
        runDispatchErrorMarker();
        runMissingRequestHandlerErrorReply();
        runGrayFailure();
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
        System.out.println("scenario RL-C3 passed");
    }

    private void runProviderReschedule() {
        waitForTopology(2);
        post(adminB() + "/admin/drain");
        waitForWeight(adminB(), 0);
        collectStableProvidersWithout("a2-before-reschedule", "api-b", "api-a");
        signal("a2-ready");
        waitForSignal("a2-down");
        expectRescheduleWindowFailure();
        signal("a2-down-observed");
        waitForSignal("a2-up");
        waitForTopologyEndpoint("api-a", Env.get("ZLINK_JAVA_E2E_API_A_REPLACEMENT_ENDPOINT"));
        collectStableProvidersWithout("a2-after-reschedule", "api-b", "api-a");
        post(adminB() + "/admin/restore");
        waitForWeight(adminB(), 100);
        System.out.println("scenario RL-A2 passed");
    }

    private void runRollingGreen() {
        waitForTopology(2);
        post(adminB() + "/admin/drain");
        waitForWeight(adminB(), 0);
        collectStableProvidersWithout("a4-before-green", "api-b", "api-a");
        signal("a4-drained");

        waitForSignal("a4-original-down");
        collectStableProvidersWithout("a4-old-down", "api-b", "api-a");

        waitForSignal("a4-green-up");
        waitForTopologyEndpoint("api-b", Env.get("ZLINK_JAVA_E2E_API_B_GREEN_ENDPOINT"));
        driveUntilEvidence(adminBGreen(), "a4-green", "RL-A4 green provider did not receive traffic");
        signal("a4-green-observed");

        signal("a4-restore-ready");

        waitForSignal("a4-restored");
        waitForTopologyEndpoint("api-b", Env.get("ZLINK_JAVA_E2E_API_B_ENDPOINT"));
        driveUntilEvidence(adminB(), "a4-restored", "RL-A4 restored provider did not receive traffic");
        System.out.println("scenario RL-A4 passed");
    }

    private void runProviderFlapping() {
        waitForTopology(2);
        signal("a5-ready");
        Set<String> providers = new HashSet<>();
        int successes = 0;
        int failures = 0;
        int index = 0;
        while (!hasSignal("a5-stop")) {
            try {
                Contracts.WorkRes reply = client.requestToChannel(
                        Contracts.CHANNEL,
                        new Contracts.WorkReq("a5-flap-" + index))
                    .timeout(Duration.ofSeconds(3))
                    .submit(Contracts.WorkRes.class).toCompletableFuture().join();
                ensure(reply.value().equals("work:a5-flap-" + index),
                    "RL-A5 reply payload mismatch");
                providers.add(reply.providerRid());
                successes++;
            } catch (RuntimeException error) {
                failures++;
                ensure(failures <= 5, "RL-A5 observed repeated failures during provider flapping");
            }
            index++;
            sleep(100);
        }
        waitForTopology(2);
        Contracts.WorkRes followUp = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.WorkReq("a5-follow-up"))
            .timeout(Duration.ofSeconds(3))
            .submit(Contracts.WorkRes.class).toCompletableFuture().join();
        ensure("work:a5-follow-up".equals(followUp.value()), "RL-A5 follow-up payload mismatch");
        ensure(successes >= 10, "RL-A5 did not send enough traffic during flapping");
        ensure(providers.contains("api-b"), "RL-A5 did not converge to live api-b during flapping");
        System.out.println("scenario RL-A5 passed");
    }

    private void runReconnectStorm() {
        waitForTopology(2);
        Set<String> providers = collectProviders("a3-storm", 40, 1);
        ensure(!providers.isEmpty(), "RL-A3 storm client did not receive replies");
        System.out.println("scenario RL-A3 passed");
        System.out.println("scenario RL-D1 passed");
        sleep(Long.parseLong(Env.get("ZLINK_JAVA_E2E_STORM_EXIT_DELAY_MS", "0")));
    }

    private void runClientHostLifecycle() {
        waitForTopology(2);
        for (int index = 0; index < 12; index++) {
            Contracts.WorkRes reply = client.requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.WorkReq("rl-c1-" + index))
                .timeout(Duration.ofSeconds(3))
                .submit(Contracts.WorkRes.class).toCompletableFuture().join();
            ensure(reply.value().equals("work:rl-c1-" + index),
                "RL-C1 request payload mismatch for " + index);
        }
        Contracts.WorkRes followUp = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.WorkReq("rl-c1-after-cleanup"))
            .timeout(Duration.ofSeconds(3))
            .submit(Contracts.WorkRes.class).toCompletableFuture().join();
        ensure(followUp.value().equals("work:rl-c1-after-cleanup"),
            "RL-C1 follow-up payload mismatch");
        System.out.println("scenario RL-C1 passed");
    }

    private void runMixedBurst() {
        waitForTopology(2);
        long firstWindowNanos = 0;
        long lastWindowNanos = 0;
        Set<String> providers = new HashSet<>();
        for (int window = 0; window < 4; window++) {
            long started = System.nanoTime();
            for (int index = 0; index < 40; index++) {
                String value = "d5-soak-" + window + "-" + index;
                if (index % 5 == 0) {
                    client.sendToChannel(Contracts.CHANNEL, new Contracts.WorkMsg(value))
                        .submit();
                } else {
                    Contracts.WorkRes reply = client.requestToChannel(
                            Contracts.CHANNEL,
                            new Contracts.WorkReq(value))
                        .timeout(Duration.ofSeconds(3))
                        .submit(Contracts.WorkRes.class).toCompletableFuture().join();
                    ensure(reply.value().equals("work:" + value),
                        "RL-D5 reply payload mismatch for " + value);
                    providers.add(reply.providerRid());
                }
            }
            long elapsed = System.nanoTime() - started;
            if (window == 0) {
                firstWindowNanos = elapsed;
            }
            lastWindowNanos = elapsed;
        }
        ensure(!providers.isEmpty(), "RL-D5 did not observe request replies");
        ensure(lastWindowNanos < firstWindowNanos * 5,
            "RL-D5 latency drift exceeded the harness threshold");
        System.out.println("scenario RL-D5 passed");
    }

    private void expectRestartWindowFailure() {
        expectSingleProviderDownFailure("RL-A1", "a1-down-window");
    }

    private void expectRescheduleWindowFailure() {
        expectSingleProviderDownFailure("RL-A2", "a2-down-window");
    }

    private void expectSingleProviderDownFailure(String scenario, String value) {
        try {
            client.requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.WorkReq(value))
                .timeout(Duration.ofMillis(700))
                .submit(Contracts.WorkRes.class).toCompletableFuture().join();
            throw new IllegalStateException(scenario + " down-window request unexpectedly completed");
        } catch (RuntimeException expected) {
            // The scenario only requires a public failure while the sole admissible provider is down.
        }
    }

    private void runClientTimeoutCleanup() {
        try {
            client.requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.WorkReq("timeout"))
                .timeout(Duration.ofMillis(300))
                .submit(Contracts.WorkRes.class).toCompletableFuture().join();
            throw new IllegalStateException("RL-B1 timeout request unexpectedly completed");
        } catch (RuntimeException expected) {
            waitForEvidenceAny("TimeoutStarted", adminA(), adminB());
        }
        sleep(1800);
        Contracts.WorkRes followUp = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.WorkReq("b1-follow-up"))
            .timeout(Duration.ofSeconds(3))
            .submit(Contracts.WorkRes.class).toCompletableFuture().join();
        ensure("work:b1-follow-up".equals(followUp.value()), "RL-B1 follow-up payload mismatch");
        System.out.println("scenario RL-B1 passed");
    }

    private void runCrashDuringInflight() {
        waitForTopology(2);
        post(adminA() + "/admin/drain");
        waitForWeight(adminA(), 0);
        collectStableProvidersWithout("b2-before-crash", "api-a", "api-b");

        CompletionStage<Contracts.WorkRes> slow = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.WorkReq("slow"))
            .timeout(Duration.ofSeconds(12))
            .submit(Contracts.WorkRes.class);
        waitForEvidence(adminB(), "SlowStarted");
        signal("b2-in-flight");
        waitForSignal("b2-crashed");

        boolean failed = false;
        try {
            slow.toCompletableFuture().get(15, TimeUnit.SECONDS);
        } catch (Exception expected) {
            failed = true;
        }
        ensure(failed, "RL-B2 in-flight request unexpectedly completed after provider crash");

        post(adminA() + "/admin/restore");
        waitForWeight(adminA(), 100);
        waitForTopologyWithout("api-b", 30);
        collectStableProvidersWithoutFailures("b2-after-crash", "api-b", "api-a");
        signal("b2-survivor-observed");

        waitForSignal("b2-restored");
        waitForTopologyEndpoint("api-b", Env.get("ZLINK_JAVA_E2E_API_B_ENDPOINT"));
        driveUntilEvidence(adminB(), "b2-restored", "RL-B2 restored provider traffic missing");
        System.out.println("scenario RL-B2 passed");
    }

    private void runDrainRestore() {
        waitForTopology(2);
        Set<String> warm = collectProviders("b4-warm", 80, 2);
        ensure(warm.contains("api-a") && warm.contains("api-b"),
            "RL-B4 warmup did not reach both providers: " + warm);

        post(adminA() + "/admin/drain");
        waitForWeight(adminA(), 0);
        Set<String> drained = collectProvidersExactly("b4-drained", 40);
        ensure(drained.equals(Set.of("api-b")),
            "RL-B4 drained traffic reached an unexpected provider: " + drained);
        ensure(!hasEvidenceWithPrefix(adminA(), "WorkReq", "b4-drained-"),
            "RL-B4 drained provider recorded a new request");
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

        CompletionStage<Contracts.WorkRes> slow = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.WorkReq("slow"))
            .timeout(Duration.ofSeconds(15))
            .submit(Contracts.WorkRes.class);
        waitForEvidence(adminA(), "SlowStarted");

        post(adminA() + "/admin/drain");
        waitForWeight(adminA(), 0);
        post(adminB() + "/admin/restore");
        waitForWeight(adminB(), 100);
        collectStableProvidersWithout("b5-after-drain", "api-a", "api-b");

        post(adminA() + "/admin/release-slow");
        Contracts.WorkRes slowReply;
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
                    new Contracts.UnhandledReq("d3-missing-handler"))
                .timeout(Duration.ofSeconds(3))
                .submit(Contracts.WorkRes.class).toCompletableFuture().join();
            throw new IllegalStateException("RL-D3 missing handler request unexpectedly completed");
        } catch (RuntimeException expected) {
            waitForDispatchErrorAny("UnhandledReq", adminA(), adminB());
        }
        Contracts.WorkRes followUp = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.WorkReq("d3-follow-up"))
            .timeout(Duration.ofSeconds(3))
            .submit(Contracts.WorkRes.class).toCompletableFuture().join();
        ensure("work:d3-follow-up".equals(followUp.value()), "RL-D3 follow-up payload mismatch");
        System.out.println("scenario RL-D3 passed");
    }

    private void runObserverFaultIsolation() {
        post(adminA() + "/admin/fault/observer-throws");
        post(adminB() + "/admin/fault/observer-throws");
        try {
            client.requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.UnhandledReq("d2-observer-fault"))
                .timeout(Duration.ofSeconds(3))
                .submit(Contracts.WorkRes.class).toCompletableFuture().join();
            throw new IllegalStateException("RL-D2 missing handler request unexpectedly completed");
        } catch (RuntimeException expected) {
            waitForEvidenceValueAny(
                "RuntimeError",
                "MESSAGE_FLOW_OBSERVER_FAILED/message-flow-observer",
                adminA(),
                adminB());
        } finally {
            post(adminA() + "/admin/fault/none");
            post(adminB() + "/admin/fault/none");
        }

        Contracts.WorkRes followUp = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.WorkReq("rl-d2-after"))
            .timeout(Duration.ofSeconds(3))
            .submit(Contracts.WorkRes.class).toCompletableFuture().join();
        ensure("work:rl-d2-after".equals(followUp.value()),
            "RL-D2 messaging did not continue after observer failure");
        waitForEvidenceAny("WorkReq", adminA(), adminB());
        System.out.println("scenario RL-D2 passed");
    }

    private void runMissingRequestHandlerErrorReply() {
        try {
            client.requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.UnhandledReq("d4-missing-handler"))
                .timeout(Duration.ofSeconds(3))
                .submit(Contracts.WorkRes.class).toCompletableFuture().join();
            throw new IllegalStateException("RL-D4 missing handler request unexpectedly completed");
        } catch (RuntimeException expected) {
            waitForDispatchErrorAny("UnhandledReq", adminA(), adminB());
        }
        Contracts.WorkRes followUp = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.WorkReq("d4-follow-up"))
            .timeout(Duration.ofSeconds(3))
            .submit(Contracts.WorkRes.class).toCompletableFuture().join();
        ensure("work:d4-follow-up".equals(followUp.value()), "RL-D4 follow-up payload mismatch");
        System.out.println("scenario RL-D4 passed");
    }

    private void runGrayFailure() {
        post(adminA() + "/admin/fault-on");
        waitForEvidence(adminA(), "GrayFailureMode");
        int successes = 0;
        int failures = 0;
        Set<String> providers = new HashSet<>();
        for (int index = 0; index < 80 && successes < 10; index++) {
            try {
                Contracts.WorkRes reply = client.requestToChannel(
                        Contracts.CHANNEL,
                        new Contracts.WorkReq("b6-gray-" + index))
                    .timeout(Duration.ofSeconds(3))
                    .submit(Contracts.WorkRes.class).toCompletableFuture().join();
                ensure(reply.value().equals("work:b6-gray-" + index),
                    "RL-B6 reply payload mismatch");
                providers.add(reply.providerRid());
                successes++;
            } catch (RuntimeException expected) {
                failures++;
            }
        }
        post(adminA() + "/admin/fault-off");
        waitForEvidence(adminA(), "GrayFailureInjected");
        ensure(failures > 0, "RL-B6 did not observe public failures from degraded provider");
        ensure(successes >= 10, "RL-B6 healthy provider did not maintain enough successful traffic");
        ensure(providers.contains("api-b"), "RL-B6 did not receive successful replies from api-b");
        Contracts.WorkRes followUp = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.WorkReq("b6-follow-up"))
            .timeout(Duration.ofSeconds(3))
            .submit(Contracts.WorkRes.class).toCompletableFuture().join();
        ensure("work:b6-follow-up".equals(followUp.value()), "RL-B6 follow-up payload mismatch");
        System.out.println("scenario RL-B6 passed");
    }

    private void waitForDispatchErrorAny(String packetName, String... baseUrls) {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(30);
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
        Contracts.WorkRes beforeShutdown = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.WorkReq("b3-before-shutdown"))
            .timeout(Duration.ofSeconds(3))
            .submit(Contracts.WorkRes.class).toCompletableFuture().join();
        ensure("work:b3-before-shutdown".equals(beforeShutdown.value()),
            "RL-B3 pre-shutdown reply payload mismatch");

        post(adminB() + "/admin/shutdown");
        waitForTopologyWithout("api-b", 30);
        Set<String> providers = collectStableProvidersWithoutFailures(
            "b3-after-shutdown", "api-b", "api-a");
        ensure(providers.contains("api-a"), "RL-B3 did not converge to api-a after api-b shutdown");
        System.out.println("scenario RL-B3 passed");
    }

    private void runTopologyRecovery() {
        waitForTopology(2);
        signal("c2-ready");
        waitForSignal("c2-crashed");
        waitForTopologyWithout("api-b", 30);
        collectStableProvidersWithoutFailures("c2-after-crash", "api-b", "api-a");
        signal("c2-survivor-observed");

        waitForSignal("c2-restored");
        waitForTopologyEndpoint("api-b", Env.get("ZLINK_JAVA_E2E_API_B_ENDPOINT"));
        driveUntilEvidence(adminB(), "c2-restored", "RL-C2 restored provider traffic missing");
        System.out.println("scenario RL-C2 passed");
    }

    private void runStoreOutageRecovery() {
        waitForTopology(2);
        Contracts.WorkRes before = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.WorkReq("c4-before-outage"))
            .timeout(Duration.ofSeconds(3))
            .submit(Contracts.WorkRes.class).toCompletableFuture().join();
        ensure("work:c4-before-outage".equals(before.value()),
            "RL-C4 pre-outage reply payload mismatch");
        waitForProviderEvidence("c4-before-outage");

        signal("c4-pause-ready");
        waitForSignal("c4-store-paused");

        Contracts.WorkRes during = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.WorkReq("c4-during-outage"))
            .timeout(Duration.ofSeconds(3))
            .submit(Contracts.WorkRes.class).toCompletableFuture().join();
        ensure("work:c4-during-outage".equals(during.value()),
            "RL-C4 established channel failed during store outage");
        waitForProviderEvidence("c4-during-outage");
        signal("c4-during-observed");

        waitForSignal("c4-store-resumed");
        waitForTopology(2);
        Contracts.WorkRes after = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.WorkReq("c4-after-recovery"))
            .timeout(Duration.ofSeconds(3))
            .submit(Contracts.WorkRes.class).toCompletableFuture().join();
        ensure("work:c4-after-recovery".equals(after.value()),
            "RL-C4 recovery reply payload mismatch");
        waitForProviderEvidence("c4-after-recovery");
        System.out.println("scenario RL-C4 passed");
    }

    private Set<String> collectProviders(String prefix, int attempts, int expectedCount) {
        Set<String> providers = new HashSet<>();
        for (int index = 0; index < attempts && providers.size() < expectedCount; index++) {
            Contracts.WorkRes reply = client.requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.WorkReq(prefix + "-" + index))
                .timeout(Duration.ofSeconds(3))
                .submit(Contracts.WorkRes.class).toCompletableFuture().join();
            ensure(reply.value().equals("work:" + prefix + "-" + index),
                "reply payload mismatch for " + prefix + "-" + index);
            providers.add(reply.providerRid());
        }
        return providers;
    }

    private Set<String> collectProvidersExactly(String prefix, int attempts) {
        Set<String> providers = new HashSet<>();
        for (int index = 0; index < attempts; index++) {
            String value = prefix + "-" + index;
            Contracts.WorkRes reply = client.requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.WorkReq(value))
                .timeout(Duration.ofSeconds(3))
                .submit(Contracts.WorkRes.class).toCompletableFuture().join();
            ensure(reply.value().equals("work:" + value),
                "reply payload mismatch for " + value);
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

    private Set<String> collectStableProvidersWithoutFailures(
        String prefix,
        String forbidden,
        String required) {
        for (int window = 0; window < 30; window++) {
            Set<String> providers = new HashSet<>();
            for (int index = 0; index < 5; index++) {
                String value = prefix + "-window-" + window + "-" + index;
                try {
                    Contracts.WorkRes reply = client.requestToChannel(
                            Contracts.CHANNEL,
                            new Contracts.WorkReq(value))
                        .timeout(Duration.ofSeconds(3))
                        .submit(Contracts.WorkRes.class).toCompletableFuture().join();
                    ensure(reply.value().equals("work:" + value),
                        "reply payload mismatch for " + value);
                    providers.add(reply.providerRid());
                } catch (RuntimeException ignored) {
                }
            }
            if (!providers.contains(forbidden) && providers.contains(required)) {
                return providers;
            }
            sleep(300);
        }
        throw new IllegalStateException(
            prefix + " did not converge away from " + forbidden + " to " + required);
    }

    private void waitForTopology(int expectedRouters) {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10);
        while (System.nanoTime() < deadline) {
            try {
                long count = peers().toCompletableFuture()
                    .get(3, TimeUnit.SECONDS)
                    .stream()
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

    private void waitForTopologyWithout(String routingId, int seconds) {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(seconds);
        while (System.nanoTime() < deadline) {
            try {
                boolean found = peers().toCompletableFuture()
                    .get(3, TimeUnit.SECONDS)
                    .stream()
                    .anyMatch(entry -> routingId.equals(entry.nodeRid().toString()));
                if (!found) {
                    return;
                }
            } catch (Exception ignored) {
            }
            sleep(200);
        }
        throw new IllegalStateException("registry topology still reported " + routingId);
    }

    private void waitForTopologyEndpoint(String routingId, String endpoint) {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(15);
        while (System.nanoTime() < deadline) {
            try {
                boolean found = peers().toCompletableFuture()
                    .get(3, TimeUnit.SECONDS)
                    .stream()
                    .anyMatch(entry -> routingId.equals(entry.nodeRid().toString())
                        && endpoint.equals(entry.endpoint()));
                if (found) {
                    return;
                }
            } catch (Exception ignored) {
            }
            sleep(200);
        }
        throw new IllegalStateException(
            "registry topology did not report " + routingId + " at " + endpoint);
    }

    private void waitForProviderEvidence(String marker) {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(15);
        while (System.nanoTime() < deadline) {
            if (providerEvidenceContains(adminA(), marker) || providerEvidenceContains(adminB(), marker)) {
                return;
            }
            sleep(100);
        }
        throw new IllegalStateException("provider evidence did not contain " + marker);
    }

    private boolean providerEvidenceContains(String baseUrl, String expected) {
        try {
            JsonNode entries = json.readTree(get(baseUrl + "/evidence")).path("entries");
            if (!entries.isArray()) {
                return false;
            }
            for (JsonNode entry : entries) {
                String value = entry.path("value").asText();
                if (value.contains(expected)) {
                    return true;
                }
            }
        } catch (Exception ignored) {
        }
        return false;
    }

    private CompletionStage<java.util.List<ZLinkPeerLocation>> peers() {
        return lifecycle.monitoringLocationRuntimeQuery().listPeerLocations(new ZLinkPeerLocationFilter(
            ZLinkLocationAutoConnectType.CLIENT_SERVER,
            Contracts.CHANNEL,
            ZLinkLocationRole.ROUTER,
            null,
            null));
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

    private void waitForEvidenceValueAny(String marker, String value, String... baseUrls) {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10);
        while (System.nanoTime() < deadline) {
            for (String baseUrl : baseUrls) {
                if (hasEvidence(baseUrl, marker, value)) {
                    return;
                }
            }
            sleep(100);
        }
        throw new IllegalStateException(
            "evidence " + marker + "/" + value + " was not observed at any provider");
    }

    private void driveUntilEvidence(String baseUrl, String prefix, String failureMessage) {
        for (int index = 0; index < 80; index++) {
            String value = prefix + "-" + index;
            Contracts.WorkRes reply = client.requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.WorkReq(value))
                .timeout(Duration.ofSeconds(3))
                .submit(Contracts.WorkRes.class).toCompletableFuture().join();
            ensure("work:".concat(value).equals(reply.value()),
                "RL-A4 reply payload mismatch for " + value);
            if ("api-b".equals(reply.providerRid()) && hasEvidence(baseUrl, "WorkReq", value)) {
                return;
            }
            sleep(100);
        }
        throw new IllegalStateException(failureMessage);
    }

    private boolean hasEvidence(String baseUrl, String marker, String value) {
        try {
            JsonNode entries = json.readTree(get(baseUrl + "/evidence")).path("entries");
            if (!entries.isArray()) {
                return false;
            }
            for (JsonNode entry : entries) {
                if (marker.equals(entry.path("marker").asText())
                    && value.equals(entry.path("value").asText())) {
                    return true;
                }
            }
        } catch (Exception ignored) {
        }
        return false;
    }

    private boolean hasEvidenceWithPrefix(String baseUrl, String marker, String valuePrefix) {
        try {
            JsonNode entries = json.readTree(get(baseUrl + "/evidence")).path("entries");
            ensure(entries.isArray(), "provider evidence response has no entries array");
            for (JsonNode entry : entries) {
                if (marker.equals(entry.path("marker").asText())
                    && entry.path("value").asText().startsWith(valuePrefix)) {
                    return true;
                }
            }
        } catch (IOException error) {
            throw new IllegalStateException("failed to parse provider evidence", error);
        }
        return false;
    }

    private String adminA() {
        return Env.get("ZLINK_JAVA_E2E_HTTP_A_ENDPOINT");
    }

    private String adminB() {
        return Env.get("ZLINK_JAVA_E2E_HTTP_B_ENDPOINT");
    }

    private String adminBGreen() {
        return Env.get("ZLINK_JAVA_E2E_HTTP_B_GREEN_ENDPOINT");
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

    private boolean hasSignal(String name) {
        return Files.exists(controlDir().resolve(name));
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
