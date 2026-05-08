/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.single;

import systems.zlink.Context;
import systems.zlink.DealerSocket;
import systems.zlink.Message;
import systems.zlink.RecvException;
import systems.zlink.RecvFlags;
import systems.zlink.RecvResult;
import systems.zlink.RouterSocket;
import systems.zlink.perf.PerfUtil;
import systems.zlink.service.spot.Spot;
import systems.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

final class PerfSpotReqRep {
    private static final String CHANNEL_NAME = "perf.spot.reqrep";

    private PerfSpotReqRep() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        AtomicBoolean stopRequested = new AtomicBoolean(false);
        AtomicReference<Throwable> responderFailure = new AtomicReference<>();
        String endpoint = normalizeSpotEndpoint(
            PerfUtil.endpoint(config.transport(), "single-spot-reqrep"),
            config.transport());

        try (Context ctx = PerfUtil.newContext(config);
             SpotNode requesterNode = new SpotNode(ctx);
             DealerSocket requesterDealer = new DealerSocket(ctx);
             Spot requester = requesterNode.createSpot();
             RouterSocket responder = new RouterSocket(ctx)) {
            PerfUtil.applySocketOptions(requesterDealer, config);
            PerfUtil.applySocketOptions(responder, config);
            PerfUtil.configureClientTls(requesterDealer, config.transport());
            PerfUtil.configureServerTls(responder, config.transport());
            requesterNode.attachChannelDealerManual(CHANNEL_NAME, requesterDealer);
            responder.bind(endpoint);
            requesterDealer.connect(endpoint);

            Thread responderThread = new Thread(() -> runResponder(
                responder, config.size(), stopRequested, responderFailure),
                "single-spot-reqrep-responder");
            responderThread.start();

            if (!waitForWarmupReady(requester, config.size(),
                    config.connectReadyTimeoutMs())) {
                throw new IllegalStateException("spot reqrep ready timed out");
            }
            settleAfterReady();

            metrics.startActiveWindow();
            long activeEnd = System.nanoTime()
                + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                try (Message active = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                    PerfUtil.Header reply = requestReply(requester, active,
                        config.size(), Duration.ofSeconds(2));
                    if (reply.phase() == PerfUtil.PHASE_ACTIVE) {
                        metrics.recordNanos(reply.latencyNanos() / 2L);
                    }
                }
            }
            try (Message cooldown = PerfUtil.payload(config.size(),
                     (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
                requestReply(requester, cooldown, config.size(), Duration.ofSeconds(2));
            }

            stopRequested.set(true);
            PerfUtil.join(responderThread, "spot reqrep responder",
                Duration.ofSeconds(10));
            if (responderFailure.get() != null) {
                throw new IllegalStateException("spot reqrep responder failed",
                    responderFailure.get());
            }
            ctx.shutdown();
            return metrics.finishSingle(config);
        }
    }

    private static void runResponder(RouterSocket responder,
                                     int expectedSize,
                                     AtomicBoolean stopRequested,
                                     AtomicReference<Throwable> responderFailure) {
        try {
            while (!stopRequested.get()) {
                systems.zlink.Received received;
                try {
                    received = responder.recv(RecvFlags.DONT_WAIT);
                } catch (RecvException ex) {
                    if (ex.getResult() == RecvResult.NO_DATA) {
                        Thread.sleep(1L);
                        continue;
                    }
                    throw ex;
                }
                try (received) {
                    PerfUtil.Header header = PerfUtil.decodeHeader(
                        received.firstPart(), expectedSize);
                    if (header == null) {
                        continue;
                    }
                    try (Message reply = received.firstPart().move()) {
                        received.reply(reply);
                    }
                    if (header.phase() == PerfUtil.PHASE_COOLDOWN) {
                        stopRequested.set(true);
                    }
                }
            }
        } catch (Throwable ex) {
            responderFailure.compareAndSet(null, ex);
            stopRequested.set(true);
        }
    }

    private static boolean waitForWarmupReady(Spot requester,
                                              int size,
                                              int timeoutMs) {
        long deadlineNs = System.nanoTime()
            + Duration.ofMillis(Math.max(1, timeoutMs)).toNanos();
        while (System.nanoTime() < deadlineNs) {
            try (Message probe = PerfUtil.payload(size,
                     (byte) PerfUtil.PHASE_WARMUP, System.nanoTime())) {
                PerfUtil.Header reply = requestReply(requester, probe, size,
                    Duration.ofMillis(200));
                if (reply.phase() == PerfUtil.PHASE_WARMUP) {
                    return true;
                }
            } catch (Throwable e) {
                System.err.println("[single-spot-reqrep] warmup probe failed: " + e);
                try {
                    Thread.sleep(10L);
                } catch (InterruptedException ex) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("spot reqrep warmup interrupted",
                        ex);
                }
            }
        }
        return false;
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
            throw new IllegalStateException("spot reqrep request failed", ex.getCause());
        } catch (TimeoutException ex) {
            throw new IllegalStateException("spot reqrep request timed out", ex);
        }
        try {
            if (replyParts.isEmpty()) {
                throw new IllegalStateException("spot reqrep reply was empty");
            }
            PerfUtil.Header header = PerfUtil.decodeHeader(replyParts.get(0), expectedSize);
            if (header == null) {
                throw new IllegalStateException("spot reqrep reply header missing");
            }
            return header;
        } finally {
            replyParts.forEach(Message::close);
        }
    }

    private static void settleAfterReady() {
        int settleMs = PerfUtil.intEnv("PERF_SINGLE_SPOT_READY_SETTLE_MS", 1000);
        if (settleMs <= 0) {
            return;
        }
        try {
            Thread.sleep(settleMs);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("spot reqrep settle interrupted", ex);
        }
    }

    private static String normalizeSpotEndpoint(String endpoint, String transport) {
        if (!"tls".equals(transport) && !"wss".equals(transport)) {
            return endpoint;
        }
        return endpoint.replace("://127.0.0.1:", "://localhost:");
    }
}
