/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.SpotDispatchEvent;
import dev.kairoscode.zlink.perf.PerfSocketPollSet;
import dev.kairoscode.zlink.perf.PerfUtil;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

final class PerfSpotReqRep {
    private PerfSpotReqRep() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        CountDownLatch ready = new CountDownLatch(1);
        AtomicReference<Throwable> callbackFailure = new AtomicReference<>();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        String endpoint = normalizeSpotEndpoint(
            PerfUtil.endpoint(config.transport(), "single-spot-reqrep"),
            config.transport());

        try (Context ctx = PerfUtil.newContext(config);
             RouterSocket requester = new RouterSocket(ctx);
             SpotNode replierNode = new SpotNode(ctx);
             Spot replier = replierNode.createSpot();
             PerfSocketPollSet requesterPollSet = PerfSocketPollSet.fromSockets(
                 List.of(requester), PollEventType.POLLIN.getValue())) {
            PerfUtil.applySpotOptions(replierNode, config);
            PerfUtil.configureServerTls(replierNode, config.transport());
            PerfUtil.configureClientTls(requester, config.transport());
            PerfUtil.applySocketOptions(requester, config);
            replierNode.bind(endpoint);
            requester.connect(endpoint);

            replier.onDispatchEvent(info -> {
                if (info.event() != SpotDispatchEvent.ROUTED_READABLE) {
                    return;
                }
                try {
                    while (true) {
                        dev.kairoscode.zlink.Received received;
                        try {
                            received = replier.recvRouted(RecvFlags.DONT_WAIT);
                        } catch (dev.kairoscode.zlink.RecvException ex) {
                            if (ex.getResult() == dev.kairoscode.zlink.RecvResult.NO_DATA) {
                                return;
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
                        }
                    }
                } catch (Throwable ex) {
                    callbackFailure.compareAndSet(null, ex);
                    ready.countDown();
                }
            });

            long deadline = System.nanoTime() + Duration.ofSeconds(10).toNanos();
            while (ready.getCount() > 0L && System.nanoTime() < deadline) {
                try (Message probe = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_WARMUP, System.nanoTime())) {
                    requester.sendToSpot(replierNode.routingId(), replier.routingId(), probe);
                    PerfUtil.Header reply = awaitReply(requester, requesterPollSet, config,
                        System.nanoTime() + Duration.ofMillis(
                            Math.max(1, config.connectReadyTimeoutMs())).toNanos());
                    if (reply.phase() == PerfUtil.PHASE_WARMUP) {
                        ready.countDown();
                    }
                }
            }
            PerfUtil.await(ready, "spot reqrep ready", Duration.ofSeconds(10));
            settleAfterReady();

            metrics.startActiveWindow();
            long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                try (Message active = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                    requester.sendToSpot(replierNode.routingId(), replier.routingId(), active);
                    PerfUtil.Header reply = awaitReply(requester, requesterPollSet, config,
                        activeEnd + Duration.ofSeconds(2).toNanos());
                    if (reply.phase() == PerfUtil.PHASE_ACTIVE) {
                        metrics.recordNanos(reply.latencyNanos() / 2L);
                    }
                }
            }
            try (Message cooldown = PerfUtil.payload(config.size(),
                     (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
                requester.sendToSpot(replierNode.routingId(), replier.routingId(), cooldown);
                awaitReply(requester, requesterPollSet, config,
                    System.nanoTime() + Duration.ofSeconds(2).toNanos());
            }

            if (callbackFailure.get() != null) {
                throw new IllegalStateException("spot reqrep replier failed",
                    callbackFailure.get());
            }
            return metrics.finishSingle(config);
        }
    }

    private static PerfUtil.Header awaitReply(RouterSocket requester,
                                              PerfSocketPollSet pollSet,
                                              PerfUtil.Config config,
                                              long deadlineNs) {
        while (System.nanoTime() < deadlineNs) {
            long remainingNs = Math.max(1L, deadlineNs - System.nanoTime());
            int timeoutMs = Math.max(1, (int) Math.min(Integer.MAX_VALUE,
                Duration.ofNanos(remainingNs).toMillis()));
            if (pollSet.poll(timeoutMs) <= 0
                || !pollSet.isReady(0, PollEventType.POLLIN.getValue())) {
                continue;
            }
            while (true) {
                dev.kairoscode.zlink.Received received = PerfUtil.recvNoWait(requester);
                if (received == null) {
                    break;
                }
                try (received) {
                    PerfUtil.Header header = PerfUtil.decodeHeader(
                        received.firstPart(), config.size());
                    if (header != null) {
                        return header;
                    }
                }
            }
        }
        throw new IllegalStateException("spot reqrep reply timed out");
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
