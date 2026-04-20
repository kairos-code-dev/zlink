/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.SpotDispatchEvent;
import dev.kairoscode.zlink.perf.PerfUtil;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

final class PerfSpotReqRep {
    private PerfSpotReqRep() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        CountDownLatch ready = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        String endpoint = normalizeSpotEndpoint(
            PerfUtil.endpoint(config.transport(), "single-spot-reqrep"),
            config.transport());

        try (Context ctx = PerfUtil.newContext(config);
             RouterSocket requester = new RouterSocket(ctx);
             SpotNode replierNode = new SpotNode(ctx);
             Spot replier = replierNode.createSpot()) {
            PerfUtil.applySpotOptions(replierNode, config);
            PerfUtil.configureServerTls(replierNode, config.transport());
            PerfUtil.configureClientTls(requester, config.transport());
            replierNode.bind(endpoint);
            requester.connect(endpoint);

            replier.onDispatchEvent(event -> {
                if (event != SpotDispatchEvent.ROUTED_READABLE) {
                    return;
                }
                try {
                    while (true) {
                        dev.kairoscode.zlink.Received received;
                        try {
                            received = replier.recvRouted(dev.kairoscode.zlink.RecvFlags.DONT_WAIT);
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
                            if (header.phase() == PerfUtil.PHASE_WARMUP
                                && ready.getCount() > 0L) {
                                ready.countDown();
                            }
                            try (Message reply = received.firstPart().move()) {
                                received.reply(reply);
                            }
                        }
                    }
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    ready.countDown();
                }
            });

            try (Message probe = PerfUtil.payloadTemplate(config.size())) {
                long deadline = System.nanoTime() + Duration.ofSeconds(10).toNanos();
                while (ready.getCount() > 0L && System.nanoTime() < deadline) {
                    PerfUtil.writePayload(probe, config.size(),
                        (byte) PerfUtil.PHASE_WARMUP, System.nanoTime());
                    closeReply(requestToSpot(requester, replierNode, replier, probe,
                        Duration.ofMillis(config.connectReadyTimeoutMs())));
                    try {
                        ready.await(Math.max(1L, config.recvTimeoutMs()), TimeUnit.MILLISECONDS);
                    } catch (InterruptedException ex) {
                        Thread.currentThread().interrupt();
                        throw new IllegalStateException("spot reqrep probe interrupted", ex);
                    }
                }
            }
            PerfUtil.await(ready, "spot reqrep ready", Duration.ofSeconds(10));
            settleAfterReady();

            try (Message active = PerfUtil.payloadTemplate(config.size());
                 Message cooldown = PerfUtil.payloadTemplate(config.size())) {
                metrics.startActiveWindow();
                long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
                while (System.nanoTime() < activeEnd) {
                    PerfUtil.writePayload(active, config.size(),
                        (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime());
                    List<Message> reply = requestToSpot(requester, replierNode, replier, active,
                        Duration.ofSeconds(2));
                    try {
                        if (reply.isEmpty()) {
                            continue;
                        }
                        PerfUtil.Header header = PerfUtil.decodeHeader(reply.get(0), config.size());
                        if (header != null && header.phase() == PerfUtil.PHASE_ACTIVE) {
                            metrics.recordNanos(header.latencyNanos() / 2L);
                        }
                    } finally {
                        closeReply(reply);
                    }
                }
                PerfUtil.writePayload(cooldown, config.size(),
                    (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime());
                closeReply(requestToSpot(requester, replierNode, replier, cooldown,
                    Duration.ofSeconds(2)));
            }

            if (failure.get() != null) {
                throw new IllegalStateException("spot reqrep replier failed", failure.get());
            }
            return metrics.finishSingle(config);
        }
    }

    private static List<Message> requestToSpot(RouterSocket requester,
                                               SpotNode replierNode,
                                               Spot replier,
                                               Message request,
                                               Duration timeout) {
        try {
            return requester.requestToSpot(replierNode.routingId(), replier.routingId(),
                List.of(Message.copyOf(request)), timeout)
                .get(timeout.toMillis(), TimeUnit.MILLISECONDS);
        } catch (java.util.concurrent.ExecutionException ex) {
            throw new IllegalStateException("spot reqrep request failed", ex.getCause());
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("spot reqrep request interrupted", ex);
        } catch (java.util.concurrent.TimeoutException ex) {
            throw new IllegalStateException("spot reqrep request timed out", ex);
        }
    }

    private static void closeReply(List<Message> reply) {
        for (Message part : reply) {
            if (part != null) {
                part.close();
            }
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
        if (!"tls".equals(transport)) {
            return endpoint;
        }
        return endpoint.replace("://127.0.0.1:", "://localhost:");
    }
}
