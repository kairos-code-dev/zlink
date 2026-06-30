package systems.zlink.e2e.pubsub.client.Support;

import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import systems.zlink.e2e.pubsub.shared.Contracts;

public final class ScenarioAssert {
    private static final Duration EVIDENCE_TIMEOUT = Duration.ofSeconds(15);

    private ScenarioAssert() {
    }

    public static void waitForAnyEvent(Evidence evidence, String subscriberRid, String scenario) {
        waitUntil(() -> evidence.snapshot(subscriberRid).entries().stream()
            .anyMatch(entry -> scenario.equals(entry.scenario())));
    }

    public static void waitForEvent(
        Evidence evidence,
        String subscriberRid,
        String scenario,
        int sequence) {
        waitUntil(() -> hasEvent(evidence.snapshot(subscriberRid), scenario, sequence));
    }

    public static void waitForDispatchError(
        Evidence evidence,
        String subscriberRid,
        String packetName) {
        waitUntil(() -> evidence.snapshot(subscriberRid).entries().stream()
            .anyMatch(entry -> "DispatchError".equals(entry.marker())
                && entry.value().contains("HANDLER_MISSING")
                && entry.value().contains("DROP")
                && entry.value().contains(packetName)));
    }

    public static List<Integer> commonSequences(
        Evidence evidence,
        String scenario,
        Set<String> subscriberRids) {
        long deadline = System.nanoTime() + EVIDENCE_TIMEOUT.toNanos();
        while (System.nanoTime() < deadline) {
            List<Integer> common = null;
            for (String rid : subscriberRids) {
                List<Integer> sequences = evidence.snapshot(rid).entries().stream()
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

    public static boolean hasEvent(
        Contracts.EvidenceSnapshot snapshot,
        String scenario,
        int sequence) {
        return snapshot.entries().stream()
            .anyMatch(entry -> "EventMsg".equals(entry.marker())
                && scenario.equals(entry.scenario())
                && entry.sequence() == sequence);
    }

    public static boolean hasContiguousRun(List<Integer> sequences, int minLength) {
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

    public static void waitUntil(Check check) {
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

    public static void touch(String file) {
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

    public static void waitForFile(String file) {
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

    public static void ensure(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }

    public static void sleep(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted", error);
        }
    }

    @FunctionalInterface
    public interface Check {
        boolean ok();
    }
}
