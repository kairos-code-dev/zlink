package systems.zlink.framework.testkit;

import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Arrays;
import java.util.Locale;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

/**
 * Admission burst benchmark for the Entry Spot actor dispatch path.
 *
 * <p>Drives N actors x M packets through an Entry Spot actor send handler on the
 * fake backend and reports wall time plus per-packet dispatch latency percentiles.
 * The numbers are written to stdout so they can be captured as a baseline before
 * and after Entry Spot dispatch changes.
 */
final class EntrySpotAdmissionBurstBenchmark {
    private static final int ACTORS = 200;
    private static final int PACKETS_PER_ACTOR = 500;

    @Test
    void entrySpotAdmissionBurst() throws Exception {
        // Warmup pass to settle JIT and runtime startup costs.
        runBurst(40, 10);
        BurstResult warm = runBurst(ACTORS, PACKETS_PER_ACTOR);
        BurstResult best = warm;
        for (int round = 0; round < 2; round++) {
            BurstResult next = runBurst(ACTORS, PACKETS_PER_ACTOR);
            if (next.wallNanos() < best.wallNanos()) {
                best = next;
            }
        }
        System.out.println(format("entry-spot-admission-burst[best-of-3]", best));
        assertTrue(best.completed(), "benchmark burst did not complete");
    }

    private static String format(String label, BurstResult result) {
        double wallMs = result.wallNanos() / 1_000_000.0d;
        double throughput = result.total() / (result.wallNanos() / 1_000_000_000.0d);
        return String.format(
            Locale.ROOT,
            "%s actors=%d packetsPerActor=%d total=%d wallMs=%.3f throughputPerSec=%.0f "
                + "p50Us=%.1f p95Us=%.1f p99Us=%.1f maxUs=%.1f",
            label,
            ACTORS,
            PACKETS_PER_ACTOR,
            result.total(),
            wallMs,
            throughput,
            result.percentileUs(50),
            result.percentileUs(95),
            result.percentileUs(99),
            result.percentileUs(100));
    }

    private static BurstResult runBurst(int actorCount, int packetsPerActor) throws Exception {
        int total = actorCount * packetsPerActor;
        BenchActorSendHandler.reset(total);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(EntrySpotAdmissionBurstBenchmark.class);
        { var mesh = options.addSpotMesh("bench"); { var node = mesh;
                node.enableRouter("inproc://entry-admission-bench-router");
                { var entry = node.configureEntrySpot(); entry.setRoutingId(RoutingId.from("bench-entry-spot")); };
                node.addEntrySpot(SpotRuntimeFakeBackendTest.LifecycleEntrySpot.class);
                node.addActorFactory("player", SpotRuntimeFakeBackendTest.PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            String[] actorIds = new String[actorCount];
            for (int index = 0; index < actorCount; index++) {
                actorIds[index] = "bench-player-" + index;
                runtime.actorManager()
                    .create(actorIds[index], "player")
                    .toCompletableFuture()
                    .join();
            }

            long start = System.nanoTime();
            for (int packet = 0; packet < packetsPerActor; packet++) {
                for (int index = 0; index < actorCount; index++) {
                    backendFactory.dispatchEntrySpotActorMessage(
                        actorIds[index],
                        "BenchSend",
                        Long.toString(System.nanoTime()));
                }
            }
            boolean completed = BenchActorSendHandler.latch.await(60, TimeUnit.SECONDS);
            long wallNanos = System.nanoTime() - start;
            return new BurstResult(
                completed,
                total,
                wallNanos,
                Arrays.copyOf(BenchActorSendHandler.latencies, BenchActorSendHandler.index.get()));
        }
    }

    private record BurstResult(boolean completed, int total, long wallNanos, long[] latencies) {
        BurstResult {
            Arrays.sort(latencies);
        }

        double percentileUs(int percentile) {
            if (latencies.length == 0) {
                return Double.NaN;
            }
            int rank = Math.min(
                latencies.length - 1,
                Math.max(0, (int) Math.ceil(percentile / 100.0d * latencies.length) - 1));
            return latencies[rank] / 1_000.0d;
        }
    }

    @ZLinkHandlerGroup("bench")
    public static final class BenchActorSendHandler {
        static volatile CountDownLatch latch = new CountDownLatch(0);
        static volatile long[] latencies = new long[0];
        static final AtomicInteger index = new AtomicInteger();

        static void reset(int total) {
            latch = new CountDownLatch(total);
            latencies = new long[total];
            index.set(0);
        }

        @ZLinkSpotActorSend(packetName = "BenchSend")
        public java.util.concurrent.CompletionStage<Void> send(
            SpotRuntimeFakeBackendTest.PlayerActor actor,
            String message) {
            long now = System.nanoTime();
            long sentAt = Long.parseLong(message);
            int slot = index.getAndIncrement();
            long[] sink = latencies;
            if (slot < sink.length) {
                sink[slot] = now - sentAt;
            }
            latch.countDown();
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        }
    }
}
