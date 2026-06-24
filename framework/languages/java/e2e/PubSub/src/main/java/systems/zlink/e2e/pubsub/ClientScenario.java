package systems.zlink.e2e.pubsub;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import systems.zlink.framework.channels.ZLinkFanoutClient;

public final class ClientScenario {
    private static final Duration EVIDENCE_TIMEOUT = Duration.ofSeconds(15);
    private final ZLinkFanoutClient fanout;
    private final ObjectMapper json;
    private final HttpClient http = HttpClient.newHttpClient();

    public ClientScenario(ZLinkFanoutClient fanout, ObjectMapper json) {
        this.fanout = fanout;
        this.json = json;
    }

    public void run() {
        String mode = Env.get("ZLINK_JAVA_E2E_CLIENT_MODE", "default");
        if ("subscriber-restarted".equals(mode)) {
            runSubscriberRestartAfterReconnect();
            return;
        }
        if ("slow-subscriber".equals(mode)) {
            runSlowSubscriberIsolation();
            return;
        }
        if ("publisher-restarted".equals(mode)) {
            runPublisherRestartRecovery();
            return;
        }

        touch(Env.get("ZLINK_JAVA_E2E_PUBLISHER_READY_FILE"));
        waitForFile(Env.get("ZLINK_JAVA_E2E_PRELATE_CONTINUE_FILE"));

        publish("all", new Contracts.EventNotify("prelate", 0, "before-late"));
        waitForEvent("sub-1", "prelate", 0);
        waitForEvent("sub-2", "prelate", 0);
        touch(Env.get("ZLINK_JAVA_E2E_LATE_READY_FILE"));
        waitForFile(Env.get("ZLINK_JAVA_E2E_LATE_CONTINUE_FILE"));

        runFanoutBasicDelivery();
        runTopicFilter();
        runLateSubscriber();
        runMissingPacket();
    }

    private void runSubscriberRestartAfterReconnect() {
        publish("all", new Contracts.EventNotify("ps-a4-down", 1, "while-sub-1-down"));
        waitForEvent("sub-2", "ps-a4-down", 1);

        waitForFile(Env.get("ZLINK_JAVA_E2E_LATE_CONTINUE_FILE"));

        publish("all", new Contracts.EventNotify("ps-a4-after", 2, "after-sub-1-restart"));
        waitForEvent("sub-1", "ps-a4-after", 2);
        waitForEvent("sub-2", "ps-a4-after", 2);
        Contracts.EvidenceSnapshot restarted = snapshot("sub-1");
        ensure(!hasEvent(restarted, "ps-a4-down", 1),
            "PS-A4 restarted subscriber received event from disconnected interval");
        System.out.println("scenario PS-A4 passed");
    }

    private void runSlowSubscriberIsolation() {
        for (int sequence = 0; sequence < 8; sequence++) {
            publish("all", new Contracts.EventNotify("ps-b1", sequence, "slow-isolation-" + sequence));
        }
        waitForEvent("sub-2", "ps-b1", 7);
        waitForEvent("sub-3", "ps-b1", 7);
        System.out.println("scenario PS-B1 passed");
    }

    private void runPublisherRestartRecovery() {
        publish("all", new Contracts.EventNotify("ps-b2", 1, "after-publisher-restart"));
        waitForEvent("sub-1", "ps-b2", 1);
        waitForEvent("sub-2", "ps-b2", 1);
        waitForEvent("sub-3", "ps-b2", 1);
        System.out.println("scenario PS-B2 passed");
    }

    private void runFanoutBasicDelivery() {
        for (int index = 0; index < 20; index++) {
            publish("all", new Contracts.EventNotify("warmup", index, "warmup-" + index));
        }
        for (String rid : List.of("sub-1", "sub-2", "sub-3")) {
            waitForAnyEvent(rid, "warmup");
        }

        for (int sequence = 0; sequence < 12; sequence++) {
            publish("all", new Contracts.EventNotify("ps-a1", sequence, "fanout-" + sequence));
        }
        List<Integer> common = commonSequences("ps-a1", Set.of("sub-1", "sub-2", "sub-3"));
        ensure(hasContiguousRun(common, 4), "PS-A1 did not observe a shared contiguous sequence");
        System.out.println("scenario PS-A1 passed");
    }

    private void runTopicFilter() {
        publish("alpha", new Contracts.EventNotify("ps-a2", 1, "alpha-only"));
        publish("beta", new Contracts.EventNotify("ps-a2", 2, "beta-only"));
        publish("gamma", new Contracts.EventNotify("ps-a2", 3, "gamma-only"));

        waitForEvent("sub-1", "ps-a2", 1);
        waitForEvent("sub-2", "ps-a2", 2);
        waitForEvent("sub-3", "ps-a2", 3);
        sleep(500);

        Contracts.EvidenceSnapshot sub1 = snapshot("sub-1");
        Contracts.EvidenceSnapshot sub2 = snapshot("sub-2");
        Contracts.EvidenceSnapshot sub3 = snapshot("sub-3");
        ensure(hasEvent(sub1, "ps-a2", 1), "PS-A2 sub-1 missed alpha");
        ensure(!hasEvent(sub1, "ps-a2", 2) && !hasEvent(sub1, "ps-a2", 3),
            "PS-A2 sub-1 recorded an uninterested topic");
        ensure(hasEvent(sub2, "ps-a2", 2), "PS-A2 sub-2 missed beta");
        ensure(!hasEvent(sub2, "ps-a2", 1) && !hasEvent(sub2, "ps-a2", 3),
            "PS-A2 sub-2 recorded an uninterested topic");
        ensure(hasEvent(sub3, "ps-a2", 3), "PS-A2 sub-3 missed gamma");
        ensure(!hasEvent(sub3, "ps-a2", 1) && !hasEvent(sub3, "ps-a2", 2),
            "PS-A2 sub-3 recorded an uninterested topic");
        System.out.println("scenario PS-A2 passed");
    }

    private void runLateSubscriber() {
        Contracts.EvidenceSnapshot late = snapshot("sub-3");
        ensure(!hasEvent(late, "prelate", 0), "PS-A3 late subscriber received replayed pre-late event");
        publish("all", new Contracts.EventNotify("ps-a3", 1, "after-late"));
        waitForEvent("sub-3", "ps-a3", 1);
        System.out.println("scenario PS-A3 passed");
    }

    private void runMissingPacket() {
        fanout.publish(
                Contracts.EVENT_CHANNEL,
                "all",
                new Contracts.EventNotify("ps-c1", 1, "missing-packet"))
            .packetName("MissingEventNotify")
            .await();
        waitForDispatchError("sub-1", "MissingEventNotify");
        publish("all", new Contracts.EventNotify("ps-c1", 2, "normal-after-missing"));
        waitForEvent("sub-1", "ps-c1", 2);
        waitForEvent("sub-2", "ps-c1", 2);
        waitForEvent("sub-3", "ps-c1", 2);
        System.out.println("scenario PS-C1 passed");
    }

    private void publish(String topic, Contracts.EventNotify message) {
        fanout.publish(Contracts.EVENT_CHANNEL, topic, message)
            .packetName(Contracts.EVENT_PACKET)
            .await();
    }

    private List<Integer> commonSequences(String scenario, Set<String> subscriberRids) {
        long deadline = System.nanoTime() + EVIDENCE_TIMEOUT.toNanos();
        while (System.nanoTime() < deadline) {
            List<Integer> common = null;
            for (String rid : subscriberRids) {
                List<Integer> sequences = snapshot(rid).entries().stream()
                    .filter(entry -> scenario.equals(entry.scenario()))
                    .map(Contracts.EvidenceEntry::sequence)
                    .distinct()
                    .sorted()
                    .toList();
                if (common == null) {
                    common = new ArrayList<>(sequences);
                } else {
                    common.retainAll(sequences);
                }
            }
            if (common != null && hasContiguousRun(common, 4)) {
                return common;
            }
            sleep(100);
        }
        return List.of();
    }

    private void waitForAnyEvent(String subscriberRid, String scenario) {
        waitUntil(() -> snapshot(subscriberRid).entries().stream()
            .anyMatch(entry -> scenario.equals(entry.scenario())));
    }

    private void waitForEvent(String subscriberRid, String scenario, int sequence) {
        waitUntil(() -> hasEvent(snapshot(subscriberRid), scenario, sequence));
    }

    private void waitForDispatchError(String subscriberRid, String packetName) {
        waitUntil(() -> snapshot(subscriberRid).entries().stream()
            .anyMatch(entry -> "DispatchError".equals(entry.marker())
                && entry.value().contains("HANDLER_MISSING")
                && entry.value().contains("DROP")
                && entry.value().contains(packetName)));
    }

    private Contracts.EvidenceSnapshot snapshot(String subscriberRid) {
        String endpoint = switch (subscriberRid) {
            case "sub-1" -> Env.get("ZLINK_JAVA_E2E_SUB1_HTTP");
            case "sub-2" -> Env.get("ZLINK_JAVA_E2E_SUB2_HTTP");
            case "sub-3" -> Env.get("ZLINK_JAVA_E2E_SUB3_HTTP");
            default -> throw new IllegalArgumentException("unknown subscriber " + subscriberRid);
        };
        try {
            HttpRequest request = HttpRequest.newBuilder(URI.create(endpoint + "/evidence"))
                .timeout(Duration.ofSeconds(3))
                .GET()
                .build();
            HttpResponse<String> response = http.send(request, HttpResponse.BodyHandlers.ofString());
            return json.readValue(response.body(), Contracts.EvidenceSnapshot.class);
        } catch (Exception error) {
            throw new IllegalStateException("failed to fetch evidence from " + subscriberRid, error);
        }
    }

    private static boolean hasEvent(
        Contracts.EvidenceSnapshot snapshot,
        String scenario,
        int sequence) {
        return snapshot.entries().stream()
            .anyMatch(entry -> "EventNotify".equals(entry.marker())
                && scenario.equals(entry.scenario())
                && entry.sequence() == sequence);
    }

    private static boolean hasContiguousRun(List<Integer> sequences, int minLength) {
        List<Integer> sorted = sequences.stream()
            .distinct()
            .sorted(Comparator.naturalOrder())
            .toList();
        int run = 0;
        int previous = Integer.MIN_VALUE;
        for (int value : sorted) {
            run = value == previous + 1 ? run + 1 : 1;
            if (run >= minLength) {
                return true;
            }
            previous = value;
        }
        return false;
    }

    private static void waitUntil(Check check) {
        long deadline = System.nanoTime() + EVIDENCE_TIMEOUT.toNanos();
        Throwable last = null;
        while (System.nanoTime() < deadline) {
            try {
                if (check.ok()) {
                    return;
                }
            } catch (Throwable error) {
                last = error;
            }
            sleep(100);
        }
        throw new IllegalStateException("timed out waiting for evidence", last);
    }

    private static void touch(String file) {
        if (file == null || file.isBlank()) {
            return;
        }
        try {
            Files.createFile(Path.of(file));
        } catch (java.nio.file.FileAlreadyExistsException ignored) {
        } catch (Exception error) {
            throw new IllegalStateException("failed to create marker " + file, error);
        }
    }

    private static void waitForFile(String file) {
        if (file == null || file.isBlank()) {
            return;
        }
        Path path = Path.of(file);
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(30);
        while (System.nanoTime() < deadline) {
            if (Files.exists(path)) {
                return;
            }
            sleep(100);
        }
        throw new IllegalStateException("timed out waiting for marker " + file);
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

    @FunctionalInterface
    private interface Check {
        boolean ok();
    }
}
