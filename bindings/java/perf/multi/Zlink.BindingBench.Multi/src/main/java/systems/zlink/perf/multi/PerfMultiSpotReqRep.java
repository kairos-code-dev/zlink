/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.Context;
import systems.zlink.DealerSocket;
import systems.zlink.Message;
import systems.zlink.PollEventFlag;
import systems.zlink.RecvException;
import systems.zlink.RecvFlags;
import systems.zlink.RecvResult;
import systems.zlink.RouterSocket;
import systems.zlink.RoutingId;
import systems.zlink.perf.PerfControl;
import systems.zlink.perf.PerfSocketPollSet;
import systems.zlink.perf.PerfUtil;
import systems.zlink.service.spot.Spot;
import systems.zlink.service.spot.SpotNode;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

final class PerfMultiSpotReqRep {
    private static final String CHANNEL_NAME = "perf.spot.reqrep";

    private PerfMultiSpotReqRep() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        AtomicBoolean stopRequested = new AtomicBoolean(false);
        int cooldownSeen = 0;
        Thread watcher = startStopWatcher(stopRequested);
        try (Context ctx = PerfUtil.newContext(config);
             RouterSocket responder = new RouterSocket(ctx);
             PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                 List.of((systems.zlink.Socket) responder),
                 PollEventFlag.POLLIN)) {
            PerfUtil.applySocketOptions(responder, config);
            PerfUtil.configureServerTls(responder, config.transport());
            responder.bind(config.endpoint());
            PerfControl.emitReady(config.endpoint());

            while (!stopRequested.get()) {
                if (pollSet.poll(20) <= 0
                    || !pollSet.isReady(0, PollEventFlag.POLLIN)) {
                    continue;
                }
                for (;;) {
                    systems.zlink.Received received;
                    try {
                        received = responder.recv(RecvFlags.DONT_WAIT);
                    } catch (RecvException ex) {
                        if (ex.getResult() == RecvResult.NO_DATA
                            || ex.getResult() == RecvResult.BUSY) {
                            break;
                        }
                        throw ex;
                    }

                    try (received) {
                        PerfUtil.Header header = PerfUtil.decodeHeader(
                            received.firstPart(), config.size());
                        if (header == null) {
                            continue;
                        }
                        try (Message reply = received.firstPart().move()) {
                            received.reply(reply);
                        }
                        if (header.phase() == PerfUtil.PHASE_COOLDOWN
                            && ++cooldownSeen >= config.clients()) {
                            stopRequested.set(true);
                        }
                    }
                }
            }
            return PerfUtil.Result.silent(config);
        } finally {
            watcher.interrupt();
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        String endpoint = normalizeClientEndpoint(config.endpoint(),
            config.transport());
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);

        try (Context ctx = PerfUtil.newContext(config)) {
            List<ClientSlot> slots = new ArrayList<>(config.clients());
            try {
                for (int i = 0; i < config.clients(); i++) {
                    SpotNode node = new SpotNode(ctx);
                    DealerSocket dealer = new DealerSocket(ctx);
                    Spot requester = node.createSpot();
                    PerfUtil.applySocketOptions(dealer, config);
                    PerfUtil.configureClientTls(dealer, config.transport());
                    dealer.setRoutingId(RoutingId.fromBytes(
                        ("SPOT-REQREP-" + i).getBytes(StandardCharsets.UTF_8)));
                    node.attachChannelDealerManual(CHANNEL_NAME, dealer);
                    dealer.connect(endpoint);
                    slots.add(new ClientSlot(node, dealer, requester));
                }

                if (!warmupClients(slots, config.size(),
                        config.connectReadyTimeoutMs())) {
                    throw new IllegalStateException("spot reqrep ready timed out");
                }
                settleAfterReady();

                metrics.startActiveWindow();
                runClientWorkers(slots, config, metrics);
                return metrics.finishMulti(config);
            } finally {
                for (ClientSlot slot : slots) {
                    slot.close();
                }
            }
        }
    }

    private static boolean warmupClients(List<ClientSlot> slots,
                                         int size,
                                         int timeoutMs) {
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfMultiSendLoops.runClients(slots.size(), (index, durationSeconds) ->
            new Thread(() -> {
                try {
                    long deadlineNs = System.nanoTime()
                        + Duration.ofMillis(Math.max(1, timeoutMs)).toNanos();
                    while (System.nanoTime() < deadlineNs) {
                        try (Message probe = PerfUtil.payload(size,
                                 (byte) PerfUtil.PHASE_WARMUP, System.nanoTime())) {
                            PerfUtil.Header reply = requestReply(
                                slots.get(index).requester, probe, size,
                                Duration.ofMillis(200));
                            if (reply.phase() == PerfUtil.PHASE_WARMUP) {
                                return;
                            }
                        } catch (Throwable e) {
                            System.err.println("[multi-spot-reqrep] warmup probe failed: " + e);
                            sleepQuietly(10);
                        }
                    }
                    throw new IllegalStateException(
                        "spot reqrep warmup reply timed out");
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    throw new IllegalStateException(ex);
                }
            }, "multi-spot-reqrep-warmup-" + index), 1);
        return failure.get() == null;
    }

    private static void runClientWorkers(List<ClientSlot> slots,
                                         PerfUtil.Config config,
                                         PerfUtil.Metrics metrics) {
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfMultiSendLoops.runClients(slots.size(), (index, durationSeconds) ->
            new Thread(() -> {
                try {
                    ClientSlot slot = slots.get(index);
                    long activeEnd = System.nanoTime()
                        + durationSeconds * 1_000_000_000L;
                    while (System.nanoTime() < activeEnd) {
                        try (Message active = PerfUtil.payload(config.size(),
                                 (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                            PerfUtil.Header reply = requestReply(slot.requester, active,
                                config.size(), Duration.ofSeconds(2));
                            if (reply.phase() == PerfUtil.PHASE_ACTIVE) {
                                metrics.recordNanos(reply.latencyNanos() / 2L);
                            }
                        }
                    }
                    try (Message cooldown = PerfUtil.payload(config.size(),
                             (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
                        requestReply(slot.requester, cooldown, config.size(),
                            Duration.ofSeconds(2));
                    }
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    throw new IllegalStateException(ex);
                }
            }, "multi-spot-reqrep-client-" + index), config.durationSeconds());
        if (failure.get() != null) {
            throw new IllegalStateException("spot reqrep client failed",
                failure.get());
        }
    }

    private static PerfUtil.Header requestReply(Spot requester,
                                                Message payload,
                                                int expectedSize,
                                                Duration timeout) {
        List<Message> replyParts;
        try {
            replyParts = requester.requestChannel(CHANNEL_NAME)
                .message(payload)
                .timeout(timeout)
                .submitAsync()
                .get(timeout.toMillis(), TimeUnit.MILLISECONDS);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("spot reqrep request interrupted", ex);
        } catch (ExecutionException ex) {
            throw new IllegalStateException("spot reqrep request failed",
                ex.getCause());
        } catch (TimeoutException ex) {
            throw new IllegalStateException("spot reqrep request timed out", ex);
        }
        try {
            if (replyParts.isEmpty()) {
                throw new IllegalStateException("spot reqrep reply was empty");
            }
            PerfUtil.Header header = PerfUtil.decodeHeader(replyParts.get(0),
                expectedSize);
            if (header == null) {
                throw new IllegalStateException("spot reqrep reply header missing");
            }
            return header;
        } finally {
            replyParts.forEach(Message::close);
        }
    }

    private static Thread startStopWatcher(AtomicBoolean stopRequested) {
        Thread watcher = new Thread(() -> {
            try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(System.in, StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    if ("STOP".equals(line) || "QUIT".equals(line)) {
                        stopRequested.set(true);
                        return;
                    }
                }
                stopRequested.set(true);
            } catch (Exception ex) {
                throw new IllegalStateException("spot reqrep stop watcher failed",
                    ex);
            }
        }, "multi-spot-reqrep-stop");
        watcher.setDaemon(true);
        watcher.start();
        return watcher;
    }

    private static void settleAfterReady() {
        int settleMs = PerfUtil.intEnv("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000);
        if (settleMs > 0) {
            sleepQuietly(settleMs);
        }
    }

    private static void sleepQuietly(int millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("spot reqrep sleep interrupted", ex);
        }
    }

    private static String normalizeClientEndpoint(String endpoint,
                                                  String transport) {
        if (!"tls".equals(transport) && !"wss".equals(transport)) {
            return endpoint;
        }
        return endpoint.replace("://127.0.0.1:", "://localhost:");
    }

    private static final class ClientSlot implements AutoCloseable {
        private final SpotNode node;
        private final DealerSocket dealer;
        private final Spot requester;

        private ClientSlot(SpotNode node, DealerSocket dealer, Spot requester) {
            this.node = node;
            this.dealer = dealer;
            this.requester = requester;
        }

        @Override
        public void close() {
            requester.close();
            dealer.close();
            node.close();
        }
    }
}
