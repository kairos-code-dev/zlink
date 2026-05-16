/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.Message;
import systems.zlink.contracts.PollEvent;
import systems.zlink.contracts.PollEventFlag;
import systems.zlink.contracts.Poller;
import systems.zlink.contracts.RecvException;
import systems.zlink.contracts.RecvFlags;
import systems.zlink.contracts.RecvResult;
import systems.zlink.contracts.RoutingId;
import systems.zlink.contracts.SendFlags;
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
            PerfUtil.applySpotOptions(node, config);
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
            try (Message active = PerfUtil.payloadTemplate(config.size());
                 Poller poller = new Poller()) {
                poller.add(publisher, PollEventFlag.POLLOUT);
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
                    if (!publishWhenWritable(publisher, poller, active,
                            activeEnd)) {
                        break;
                    }
                    if (latencyOnly) {
                        nextSendNanos = System.nanoTime()
                            + latencyOnlyIntervalNanos;
                    }
                }
            }
            // PERF_MULTI_TEST_POLICY § 1.3.1: signal phase end with one
            // wire-level stop token.
            try (Message stop = PerfStopToken.newMessage()) {
                publisher.publish(TOPIC)
                    .message(stop)
                    .flags(SendFlags.NONE)
                    .submit();
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
            PerfUtil.applySpotOptions(node, config);
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

    private static boolean publishWhenWritable(Spot publisher,
                                               Poller poller,
                                               Message message,
                                               long deadlineNs) {
        while (System.nanoTime() < deadlineNs) {
            if (publisher.publish(TOPIC)
                    .message(message)
                    .flags(SendFlags.DONT_WAIT)
                    .submit()) {
                return true;
            }
            if (!waitFor(poller, PollEventFlag.POLLOUT, deadlineNs)) {
                return false;
            }
        }
        return false;
    }

    private static boolean waitFor(Poller poller, PollEventFlag expected,
                                   long deadlineNs) {
        List<PollEvent> events = new ArrayList<>(1);
        for (;;) {
            long remainingNs = deadlineNs - System.nanoTime();
            if (remainingNs <= 0) {
                return false;
            }
            poller.wait(events, Duration.ofMillis(-1));
            for (PollEvent event : events) {
                if (event.revents().contains(expected)) {
                    return true;
                }
            }
        }
    }

    private static void runClientSubscribers(PerfUtil.Config config,
                                             List<Spot> subscribers,
                                             PerfUtil.Metrics metrics,
                                             PerfSpotDirectControl control) {
        boolean[] cooldownSeen = new boolean[subscribers.size()];
        boolean[] queued = new boolean[subscribers.size()];
        Deque<Integer> readyQueue = new ArrayDeque<>(subscribers.size());
        CountDownLatch complete = new CountDownLatch(1);
        AtomicLong activeEndRef = new AtomicLong();
        AtomicReference<Throwable> failure = new AtomicReference<>();
        try {
            for (int i = 0; i < subscribers.size(); i++) {
                final int index = i;
                Spot subscriber = subscribers.get(i);
                subscriber.onDispatchEvent(info -> {
                    if (info != null
                        && info.event()
                        == systems.zlink.contracts.SpotDispatchEvent.SUBSCRIBE_READABLE) {
                        long activeEnd = activeEndRef.get();
                        if (activeEnd == 0L) {
                            return;
                        }
                        synchronized (readyQueue) {
                            if (!queued[index]) {
                                queued[index] = true;
                                readyQueue.addLast(index);
                            }
                            readyQueue.notifyAll();
                        }
                    }
                });
            }
            settleReadyBarrier();
            control.publishConnected();
            control.publishReadyCount(config.size(), subscribers.size());
            PerfControl.emitClientReady(config.size());
            PerfControl.awaitStart(config.size(), "spot client");
            activeEndRef.set(Long.MAX_VALUE);
            control.waitStart(config.size(), config.connectReadyTimeoutMs());
            long activeEnd = System.nanoTime()
                + Duration.ofSeconds(config.durationSeconds()).toNanos();
            activeEndRef.set(activeEnd);
            synchronized (readyQueue) {
                for (int i = 0; i < subscribers.size(); i++) {
                    queued[i] = true;
                    readyQueue.addLast(i);
                }
                readyQueue.notifyAll();
            }
            long finishDeadline = activeEnd
                + Duration.ofMillis(postPhaseSettleMs()).toNanos();
            while (System.nanoTime() < finishDeadline) {
                Throwable ex = failure.get();
                if (ex != null) {
                    throw new IllegalStateException(
                        "spot dispatch drain failed: "
                        + ex.getClass().getSimpleName()
                        + (ex.getMessage() == null ? "" : ": " + ex.getMessage()),
                        ex);
                }
                if (complete.getCount() == 0) {
                    return;
                }
                Integer index = pollReadyIndex(readyQueue, queued, finishDeadline);
                if (index == null) {
                    continue;
                }
                if (cooldownSeen[index]) {
                    continue;
                }
                try {
                    boolean drained = drainSubscriber(subscribers.get(index),
                        metrics, config.size(), activeEnd, cooldownSeen, index);
                    if (drained && !cooldownSeen[index]) {
                        enqueueReadyIndex(readyQueue, queued, index);
                    }
                } catch (Throwable drainFailure) {
                    failure.compareAndSet(null, drainFailure);
                    complete.countDown();
                }
                if (allCooldownSeen(cooldownSeen)) {
                    complete.countDown();
                    return;
                }
            }
            return;
        } catch (Throwable ex) {
            throw new IllegalStateException("spot client failed", ex);
        }
    }

    private static void enqueueReadyIndex(Deque<Integer> readyQueue,
                                          boolean[] queued,
                                          int index) {
        synchronized (readyQueue) {
            if (!queued[index]) {
                queued[index] = true;
                readyQueue.addLast(index);
                readyQueue.notifyAll();
            }
        }
    }

    private static Integer pollReadyIndex(Deque<Integer> readyQueue,
                                          boolean[] queued,
                                          long finishDeadline) {
        synchronized (readyQueue) {
            while (readyQueue.isEmpty()) {
                long remainingNs = finishDeadline - System.nanoTime();
                if (remainingNs <= 0L) {
                    return null;
                }
                try {
                    long waitMs = Math.max(1L,
                        Duration.ofNanos(remainingNs).toMillis());
                    readyQueue.wait(waitMs);
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("spot client interrupted",
                        interrupted);
                }
            }
            int index = readyQueue.removeFirst();
            queued[index] = false;
            return index;
        }
    }

    private static boolean drainSubscriber(Spot subscriber,
                                        PerfUtil.Metrics metrics,
                                        int expectedSize,
                                        long activeEnd,
                                        boolean[] cooldownSeen,
                                        int index) {
        boolean progressed = false;
        // C parity (perf_multi_spot_client.cpp drain_spot_client_slot): drain
        // until the recv would block (EAGAIN) with no fixed per-wake cap.
        while (true) {
            try (TopicMessage received = subscribeNoWait(subscriber)) {
                if (received == null) {
                    return progressed;
                }
                progressed = true;
                int phase = handleDelivery(received, metrics, expectedSize,
                    activeEnd);
                if (phase == PerfUtil.PHASE_COOLDOWN) {
                    cooldownSeen[index] = true;
                    return progressed;
                }
            }
        }
    }

    private static TopicMessage subscribeNoWait(Spot subscriber) {
        try {
            TopicMessage message = new TopicMessage();
            return subscriber.subscribe(message, RecvFlags.DONT_WAIT) ? message : null;
        } catch (RecvException ex) {
            RecvResult result = ex.getResult();
            if (result == RecvResult.NO_DATA
                || result == RecvResult.BUSY
                || result == RecvResult.TERMINATED
                || result == RecvResult.INVALID_HANDLE) {
                return null;
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
                                      long activeEnd) {
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
            if (System.nanoTime() <= activeEnd) {
                metrics.recordNanos(header.latencyNanos());
            }
            return PerfUtil.PHASE_ACTIVE;
        }
        return header.phase();
    }

    private static int postPhaseSettleMs() {
        return PerfUtil.intEnv("PERF_MULTI_SPOT_POST_PHASE_SETTLE_MS", 0);
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
