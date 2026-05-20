/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.Message;
import systems.zlink.contracts.RecvException;
import systems.zlink.contracts.RecvFlags;
import systems.zlink.contracts.RecvResult;
import systems.zlink.contracts.RoutingId;
import systems.zlink.contracts.SendFlags;
import systems.zlink.contracts.SubmitException;
import systems.zlink.contracts.SubmitResult;
import systems.zlink.contracts.TopicMessage;
import systems.zlink.perf.PerfControl;
import systems.zlink.perf.PerfStopToken;
import systems.zlink.perf.PerfUtil;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;

final class PerfMultiSpot {
    private static final String TOPIC = "bench";
    private static final String CHANNEL_NAME = "bench-svc";

    private PerfMultiSpot() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        String controlEndpoint = derivedEndpoint(config.endpoint(), 3);
        try (Context ctx = PerfUtil.newContext(config);
             SpotNode node = new SpotNode(ctx);
             Spot publisher = node.createSpot();
             PerfSpotDirectControl control = PerfSpotDirectControl.bind(
                 ctx, config, controlEndpoint, "spot-server")) {
            node.setRoutingId(routingId("z-java-multi-spot-server"));
            publisher.setRoutingId(routingId("z-java-multi-spot-server-spot"));
            PerfUtil.configureServerTls(node, config.transport());
            node.bind(config.endpoint());
            PerfControl.emitReady(config.endpoint());
            PerfControl.emitControlReady(controlEndpoint);
            awaitDirectControlStart(control, config, "spot server");
            PerfUtil.recalculateAutoHwm(ctx);
            PerfUtil.printMultiSpotNodeAutoHwm(config, node, "server");
            long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
            boolean latencyOnly = latencyOnlyMode();
            long latencyOnlyIntervalNanos = Math.max(1,
                latencyOnlyIntervalMicros()) * 1_000L;
            long nextSendNanos = System.nanoTime();
            try (Message active = PerfUtil.payloadTemplate(config.size())) {
                while (System.nanoTime() < activeEnd) {
                    if (latencyOnly) {
                        long now = System.nanoTime();
                        if (now < nextSendNanos) {
                            sleepNanos(nextSendNanos - now);
                            continue;
                        }
                    }
                    PerfUtil.resetAndWritePayload(active, config.size(),
                        (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime());
                    if (!publishWhenWritableUntil(publisher, active,
                            activeEnd, config.transport())) {
                        break;
                    }
                    if (latencyOnly) {
                        nextSendNanos = System.nanoTime()
                            + latencyOnlyIntervalNanos;
                    }
                }
            }
            // PERF_MULTI_TEST_POLICY § 1.3.1: signal phase end with one
            // wire-level stop token. Bounded non-blocking retry (never an
            // unbounded blocking submit, which hangs on wss backpressure);
            // the client recv workers also stop at their finishDeadline so a
            // dropped terminator cannot wedge the run.
            long stopDeadline = System.nanoTime() + 2_000_000_000L;
            while (System.nanoTime() < stopDeadline) {
                try (Message stop = PerfStopToken.newMessage()) {
                    if (publisher.publish(TOPIC)
                            .message(stop)
                            .flags(SendFlags.DONT_WAIT)
                            .submit()) {
                        break;
                    }
                } catch (SubmitException ex) {
                    if (!isTransientSpotSubmit(ex)) {
                        throw ex;
                    }
                }
            }
            return PerfUtil.Result.silent(config);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        String serverDataEndpoint = normalizeClientEndpoint(config.endpoint(),
            config.transport());
        String serverControlEndpoint = normalizeClientEndpoint(
            derivedEndpoint(config.endpoint(), 3), config.transport());
        String clientControlEndpoint = normalizeClientEndpoint(
            PerfUtil.endpoint(config.transport(), "multi-spot-control-client"),
            config.transport());
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        try (Context ctx = PerfUtil.newContext(config);
             SpotNode node = new SpotNode(ctx);
             PerfSpotDirectControl control = PerfSpotDirectControl.bind(
                 ctx, config, clientControlEndpoint, "spot-client")) {
            node.setRoutingId(routingId("a-java-multi-spot-client"));
            PerfUtil.configureServerTls(node, config.transport());
            PerfUtil.configureClientTls(node, config.transport());

            List<Spot> subscribers = new ArrayList<>(config.clients());
            try {
                control.connectPeer(serverControlEndpoint);
                PerfControl.emitClientControlEndpoint(clientControlEndpoint);
                PerfControl.awaitControlConnected(clientControlEndpoint,
                    "spot client");
                for (int i = 0; i < config.clients(); i++) {
                    Spot subscriber = node.createSpot();
                    subscriber.setRoutingId(routingId(
                        "a-java-multi-spot-client-spot-" + i));
                    subscribers.add(subscriber);
                }
                node.bind(normalizeClientEndpoint(PerfUtil.endpoint(
                    config.transport(), "multi-spot-client"),
                    config.transport()));
                node.connectPeer(serverDataEndpoint);
                for (Spot subscriber : subscribers) {
                    subscriber.setSubscription(TOPIC);
                }
                PerfUtil.recalculateAutoHwm(ctx);
                PerfUtil.printMultiSpotNodeAutoHwm(config, node, "client");
                runClientSubscribers(config, subscribers, metrics, control);
                return metrics.finishMulti(config);
            } finally {
                for (Spot subscriber : subscribers) {
                    try {
                        subscriber.close();
                    } catch (RuntimeException ignored) {
                    }
                }
            }
        }
    }

    // C parity: perf_multi_spot_server.cpp try_publish_locked +
    // wait_for_spot_send_progress. C publishes ZLINK_DONTWAIT and, on
    // EAGAIN, pushes the SAME message through (a SEND_FLAGS_NONE submit
    // bounded by the socket SNDTIMEO, with a POLLOUT settle) - it never
    // drops. SpotNode exposes no SNDTIMEO, so an unbounded blocking submit
    // would hang the server on persistent tls/wss backpressure
    // (server_exit_124 -> status=partial). Use the converged bounded
    // equivalent (PerfSpot.publishWhenWritableUntil): re-stamped DONTWAIT
    // retry with a sub-millisecond park, never dropping, never blocking
    // unbounded, terminated by the active deadline.
    private static boolean publishWhenWritableUntil(Spot publisher,
                                                    Message message,
                                                    long deadlineNanos,
                                                    String transport) {
        while (System.nanoTime() < deadlineNanos) {
            if (tryPublish(publisher, message, SendFlags.DONT_WAIT)) {
                return true;
            }
            if ("tcp".equals(transport)
                && tryPublish(publisher, message, SendFlags.NONE)) {
                return true;
            }
            java.util.concurrent.locks.LockSupport.parkNanos(200_000L);
        }
        return false;
    }

    private static boolean tryPublish(Spot publisher, Message message,
                                      SendFlags flags) {
        try {
            return publisher.publish(TOPIC)
                .message(message)
                .flags(flags)
                .submit();
        } catch (SubmitException ex) {
            if (isTransientSpotSubmit(ex)) {
                return false;
            }
            throw ex;
        }
    }

    private static boolean isTransientSpotSubmit(SubmitException ex) {
        return ex.getResult() == SubmitResult.BACKPRESSURED
            || ex.getResult() == SubmitResult.NOT_CONNECTED;
    }

    // C parity: perf_multi_spot_client.cpp resolve_spot_recv_worker_count
    // (~337-350). For N slots use max(4, min(128, (N+15)/16)) busy recv
    // worker threads, capped at N.
    private static int resolveRecvWorkerCount(int slotCount) {
        if (slotCount == 0) {
            return 0;
        }
        int configured = PerfUtil.intEnv("PERF_MULTI_SPOT_RECV_WORKERS", 0);
        if (configured > 0) {
            return Math.min(slotCount, configured);
        }
        int scaled = Math.max(4, Math.min(128, (slotCount + 15) / 16));
        return Math.min(slotCount, scaled);
    }

    private static void runClientSubscribers(PerfUtil.Config config,
                                             List<Spot> subscribers,
                                             PerfUtil.Metrics metrics,
                                             PerfSpotDirectControl control) {
        int n = subscribers.size();
        boolean[] cooldownSeen = new boolean[n];
        AtomicReference<Throwable> failure = new AtomicReference<>();
        try {
            settleReadyBarrier();
            control.publishConnected();
            control.publishReadyCount(config.size(), n);
            PerfControl.emitClientReady(config.size());
            PerfControl.awaitStart(config.size(), "spot client");
            control.waitStart(config.size(), config.connectReadyTimeoutMs());
            long activeDurationNanos =
                Duration.ofSeconds(config.durationSeconds()).toNanos();
            long drainGraceNanos = drainGraceNanos(activeDurationNanos,
                config.size());
            long postSettleNanos =
                Duration.ofMillis(postPhaseSettleMs(config.size())).toNanos();
            long initialDeadline = System.nanoTime() + activeDurationNanos
                + drainGraceNanos + postSettleNanos;
            AtomicBoolean stopWorkers = new AtomicBoolean(false);
            AtomicLong senderWindowEndNanos = new AtomicLong();
            AtomicLong collectionDeadlineNanos = new AtomicLong(initialDeadline);

            // C parity: perf_multi_spot_client.cpp start_spot_recv_workers
            // (~853-889) + spot_client_recv_worker_loop (~832-851). A pool of
            // worker threads, each owning a fixed slice of the spot slots,
            // busy-drains every owned slot with ZLINK_DONTWAIT. The prior
            // single dispatch-queue drain processed one of the 100 spots at a
            // time, capping MULTI_SPOT throughput ~50x below C.
            int workerCount = resolveRecvWorkerCount(n);
            Thread[] workers = new Thread[workerCount];
            for (int w = 0; w < workerCount; w++) {
                final int workerId = w;
                workers[w] = new Thread(() -> {
                    TopicMessage reusable = new TopicMessage();
                    LatencySampler sampler = new LatencySampler();
                    try {
                        while (System.nanoTime()
                            < collectionDeadlineNanos.get()
                            && !stopWorkers.get()
                            && failure.get() == null) {
                            boolean progressed = false;
                            for (int i = workerId; i < n; i += workerCount) {
                                if (cooldownSeen[i]) {
                                    continue;
                                }
                                if (drainSubscriber(subscribers.get(i),
                                        metrics, config.size(),
                                        activeDurationNanos,
                                        senderWindowEndNanos,
                                        collectionDeadlineNanos,
                                        drainGraceNanos,
                                        postSettleNanos,
                                        cooldownSeen, i, reusable, sampler)) {
                                    progressed = true;
                                }
                            }
                            if (allCooldownSeen(cooldownSeen)) {
                                return;
                            }
                            if (!progressed) {
                                // C: perf_socket_poll(NULL,0,1) when idle.
                                Thread.sleep(0, 200_000);
                            }
                        }
                    } catch (InterruptedException interrupted) {
                        Thread.currentThread().interrupt();
                    } catch (Throwable ex) {
                        failure.compareAndSet(null, ex);
                    } finally {
                        reusable.close();
                    }
                }, "spot-recv-worker-" + w);
                workers[w].setDaemon(true);
            }
            for (Thread t : workers) {
                t.start();
            }
            for (Thread t : workers) {
                long remainingMs = Math.max(1L,
                    Duration.ofNanos(collectionDeadlineNanos.get()
                        - System.nanoTime())
                        .toMillis() + 1000L);
                t.join(remainingMs);
            }
            stopWorkers.set(true);
            for (Thread t : workers) {
                if (t.isAlive()) {
                    t.interrupt();
                }
            }
            for (Thread t : workers) {
                if (t.isAlive()) {
                    t.join(1000L);
                }
            }
            Throwable ex = failure.get();
            if (ex != null) {
                throw new IllegalStateException(
                    "spot recv worker failed: "
                    + ex.getClass().getSimpleName()
                    + (ex.getMessage() == null ? "" : ": " + ex.getMessage()),
                    ex);
            }
        } catch (Throwable ex) {
            throw new IllegalStateException("spot client failed", ex);
        }
    }

    private static boolean drainSubscriber(Spot subscriber,
                                        PerfUtil.Metrics metrics,
                                        int expectedSize,
                                        long activeDurationNanos,
                                        AtomicLong senderWindowEndNanos,
                                        AtomicLong collectionDeadlineNanos,
                                        long drainGraceNanos,
                                        long postSettleNanos,
                                        boolean[] cooldownSeen,
                                        int index,
                                        TopicMessage received,
                                        LatencySampler sampler) {
        boolean progressed = false;
        // C parity (perf_multi_spot_client.cpp drain_spot_client_slot): drain
        // until the recv would block (EAGAIN) with no fixed per-wake cap.
        while (true) {
            if (!subscribeNoWait(subscriber, received)) {
                return progressed;
            }
            progressed = true;
            int phase = handleDelivery(received, metrics, expectedSize,
                activeDurationNanos, senderWindowEndNanos,
                collectionDeadlineNanos, drainGraceNanos, postSettleNanos,
                sampler);
            if (phase == PerfUtil.PHASE_COOLDOWN) {
                cooldownSeen[index] = true;
                return progressed;
            }
        }
    }

    private static boolean subscribeNoWait(Spot subscriber,
                                           TopicMessage received) {
        try {
            return subscriber.subscribe(received, RecvFlags.DONT_WAIT);
        } catch (RecvException ex) {
            RecvResult result = ex.getResult();
            if (result == RecvResult.NO_DATA
                || result == RecvResult.BUSY
                || result == RecvResult.TERMINATED
                || result == RecvResult.INVALID_HANDLE) {
                return false;
            }
            throw new IllegalStateException("recv_result_" + result, ex);
        }
    }

    private static boolean allCooldownSeen(boolean[] cooldownSeen) {
        if (cooldownSeen.length == 0) {
            return false;
        }
        for (boolean seen : cooldownSeen) {
            if (!seen) {
                return false;
            }
        }
        return true;
    }

    private static int countCooldownSeen(boolean[] cooldownSeen) {
        int count = 0;
        for (boolean seen : cooldownSeen) {
            if (seen) {
                count++;
            }
        }
        return count;
    }

    private static int handleDelivery(TopicMessage received,
                                      PerfUtil.Metrics metrics,
                                      int expectedSize,
                                      long activeDurationNanos,
                                      AtomicLong senderWindowEndNanos,
                                      AtomicLong collectionDeadlineNanos,
                                      long drainGraceNanos,
                                      long postSettleNanos,
                                      LatencySampler sampler) {
        if (received == null || received.parts().isEmpty()) {
            return PerfUtil.PHASE_UNKNOWN;
        }
        Message payload = received.firstPart();
        // PERF_MULTI_TEST_POLICY § 1.3.1: per-spot stop token signals phase end.
        // Reuse the COOLDOWN return code so the existing per-spot bookkeeping
        // (cooldownSeen[]) stays unchanged.
        if (PerfStopToken.isStopTokenMessage(payload)) {
            return PerfUtil.PHASE_COOLDOWN;
        }
        PerfUtil.Header header = PerfUtil.decodeHeader(payload, expectedSize);
        if (header == null) {
            return PerfUtil.PHASE_UNKNOWN;
        }
        if (header.phase() == PerfUtil.PHASE_ACTIVE) {
            long senderWindowEnd = senderWindowEndNanos.get();
            if (senderWindowEnd == 0L) {
                long windowStart = header.sentTsNanos() > 0L
                    ? header.sentTsNanos()
                    : System.nanoTime();
                long candidateEnd = windowStart + activeDurationNanos;
                if (senderWindowEndNanos.compareAndSet(0L, candidateEnd)) {
                    senderWindowEnd = candidateEnd;
                    collectionDeadlineNanos.updateAndGet(current ->
                        Math.max(current, candidateEnd + drainGraceNanos
                            + postSettleNanos));
                } else {
                    senderWindowEnd = senderWindowEndNanos.get();
                }
            }
            if (header.sentTsNanos() <= 0L
                || header.sentTsNanos() <= senderWindowEnd) {
                metrics.recordEvent();
                if (sampler.shouldSample()) {
                    metrics.recordLatencySampleNanos(header.latencyNanos());
                }
            }
            return PerfUtil.PHASE_ACTIVE;
        }
        return header.phase();
    }

    private static final class LatencySampler {
        private final int stride = latencySampleStride();
        private long sampleIndex;

        boolean shouldSample() {
            sampleIndex++;
            return stride <= 1 || sampleIndex == 1 || sampleIndex % stride == 0;
        }
    }

    private static int latencySampleStride() {
        return Math.max(1,
            PerfUtil.intEnv("PERF_MULTI_SPOT_LATENCY_SAMPLE_STRIDE", 32));
    }

    private static long drainGraceNanos(long activeDurationNanos, int msgSize) {
        int multiplier = 1;
        if (msgSize >= 262144) {
            multiplier = 4;
        } else if (msgSize >= 131072) {
            multiplier = 2;
        }
        return activeDurationNanos * multiplier;
    }

    private static int postPhaseSettleMs(int msgSize) {
        int defaultMs = 0;
        if (msgSize >= 262144) {
            defaultMs = 2000;
        } else if (msgSize >= 131072) {
            defaultMs = 1500;
        }
        return PerfUtil.intEnv("PERF_MULTI_SPOT_POST_PHASE_SETTLE_MS",
            defaultMs);
    }

    private static boolean latencyOnlyMode() {
        String value = System.getenv("PERF_MULTI_SPOT_LATENCY_ONLY");
        return value != null && !value.isEmpty() && !"0".equals(value);
    }

    private static int latencyOnlyIntervalMicros() {
        return PerfUtil.intEnv("PERF_MULTI_SPOT_LATENCY_ONLY_INTERVAL_US",
            1000);
    }

    private static void settleReadyBarrier() {
        int settleMs = PerfUtil.intEnv("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000);
        if (settleMs > 0) {
            sleepQuietly(settleMs);
        }
        int controlSettleMs = PerfUtil.intEnv(
            "PERF_MULTI_SPOT_CONTROL_SETTLE_MS", 25);
        if (controlSettleMs > 0) {
            sleepQuietly(controlSettleMs);
        }
    }

    private static void awaitDirectControlStart(PerfSpotDirectControl control,
                                                PerfUtil.Config config,
                                                String label) {
        String expectedStart = "START," + config.size();
        try (java.io.BufferedReader reader = new java.io.BufferedReader(
                 new java.io.InputStreamReader(System.in,
                     StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                if (line.startsWith("CONNECT_CONTROL,")) {
                    String endpoint = line.substring("CONNECT_CONTROL,".length());
                    control.connectPeer(endpoint);
                    PerfControl.emitControlConnected(endpoint);
                    break;
                }
            }
            control.waitReadyCount(config.size(), config.clients(),
                config.connectReadyTimeoutMs());
            while ((line = reader.readLine()) != null) {
                if (expectedStart.equals(line)) {
                    control.publishStart(config.size());
                    return;
                }
            }
        } catch (java.io.IOException ex) {
            throw new IllegalStateException(label + " control read failed", ex);
        }
        throw new IllegalStateException(label + " missing " + expectedStart);
    }

    private static void sleepQuietly(int millis) {
        sleepQuietly((long) millis);
    }

    private static void sleepQuietly(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("spot ready barrier interrupted", ex);
        }
    }

    private static void sleepNanos(long nanos) {
        try {
            long millis = nanos / 1_000_000L;
            int extraNanos = (int) (nanos % 1_000_000L);
            Thread.sleep(millis, extraNanos);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("spot ready barrier interrupted",
                ex);
        }
    }

    private static String normalizeClientEndpoint(String endpoint, String transport) {
        if (!"tls".equals(transport) && !"wss".equals(transport)) {
            return endpoint;
        }
        return endpoint.replace("://127.0.0.1:", "://localhost:");
    }

    private static String derivedEndpoint(String endpoint, int portOffset) {
        int schemeSep = endpoint.indexOf("://");
        int colon = endpoint.lastIndexOf(':');
        if (schemeSep <= 0 || colon <= schemeSep + 2 || colon == endpoint.length() - 1) {
            throw new IllegalArgumentException("cannot derive endpoint from: " + endpoint);
        }
        int port = Integer.parseInt(endpoint.substring(colon + 1));
        return endpoint.substring(0, colon + 1) + (port + portOffset);
    }

    private static RoutingId routingId(String value) {
        return RoutingId.fromBytes(value.getBytes(StandardCharsets.UTF_8));
    }
}
