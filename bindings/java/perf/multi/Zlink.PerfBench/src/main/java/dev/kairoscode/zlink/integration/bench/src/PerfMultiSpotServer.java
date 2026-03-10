/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.integration.bench.common.PerfCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiClientHelpers;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiMetricHeader;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiTls;
import java.lang.foreign.MemorySegment;
import dev.kairoscode.zlink.options.SocketOptions;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import dev.kairoscode.zlink.service.spot.SpotNodeSocketRole;

/**
 * MULTI_SPOT server benchmark.
 * Spot uses service-instance poller registration with facade publish.
 */
public final class PerfMultiSpotServer {
    private static final String PATTERN = "MULTI_SPOT";
    private static final String SERVICE_NAME = "perf-spot";
    private static final String TOPIC = "bench";
    private static final int MIN_PAYLOAD_BYTES = 32;
    private static final long NANOSECONDS_PER_SECOND = 1_000_000_000L;
    private static final long NANOSECONDS_PER_MILLISECOND = 1_000_000L;
    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

    private static final class PendingPublish {
        boolean pending;
        long seq;

        PendingPublish(long seq) {
            this.seq = seq;
            this.pending = false;
        }

        void begin() {
            pending = true;
        }

        void complete() {
            pending = false;
        }
    }

    private PerfMultiSpotServer() {
    }

    public static int runServer(String transport, int msgSize) {
        if (!PerfMultiClientHelpers.isSupportedTransport(PATTERN, transport)) {
            PerfCommon.printUnsupported(PATTERN, transport, msgSize,
                "unsupported transport");
            return 0;
        }

        Endpoints endpoints = resolveEndpoints(transport, "multi-spot");
        int payloadSize = Math.max(msgSize, MIN_PAYLOAD_BYTES);
        int connectTimeoutMs = Math.max(5000,
            PerfMultiCommon.resolveConnectReadyTimeoutMs() * 3);
        int warmupSeconds = PerfMultiCommon.resolveWarmupSeconds();
        int durationSeconds = PerfMultiCommon.resolveDurationSeconds();
        int settleMs = PerfMultiCommon.resolveSettleMs();
        int pollTimeoutMs = Math.max(1,
            PerfMultiCommon.resolveClientPollTimeoutMs());
        int clients = Math.max(1, PerfMultiCommon.resolveClients(PATTERN));

        try (Context context = new Context();
             Registry registry = new Registry(context);
             SpotNode node = new SpotNode(context)) {
            PerfCommon.applyServerContextOptions(context);
            registry.setEndpoints(endpoints.registryPub(), endpoints.registryRouter());
            registry.setHeartbeat(5000, 120000);
            registry.setBroadcastInterval(Math.max(100, settleMs));
            registry.start();

            applySpotNodeOptions(node);
            PerfMultiTls.configureSpotPublisherTlsIfNeeded(node, transport);
            PerfMultiTls.configureSpotSubscriberTlsIfNeeded(node, transport);
            node.bind(endpoints.serverEndpoint());
            node.register(SERVICE_NAME, endpoints.serverEndpoint());
            System.out.println("READY," + endpoints.serverEndpoint() + "|"
                + endpoints.registryPub() + "|" + endpoints.registryRouter());
            waitPubPeers(node, clients, connectTimeoutMs);

            try (Spot publisher = new Spot(node);
                 Spot.PreparedTopic topic = publisher.prepareTopic(TOPIC);
                 Spot.PublishContext publishContext =
                     publisher.createPublishContext();
                 Message payloadMessage = new Message(payloadSize);
                 Poller poller = new Poller()) {
                poller.addSpotPub(publisher, 0);

                MemorySegment payloadSegment = payloadMessage.dataSegment();
                int runId = (int) (PerfMultiMetricHeader.nowUs()
                    & 0x7FFF_FFFFL);
                PendingPublish pending = new PendingPublish(1L);

                // --- Warmup ---
                long nextSeq = runPublishPhase(publisher, topic, publishContext,
                    payloadMessage, payloadSegment, poller, msgSize, runId,
                    PerfMultiMetricHeader.PHASE_WARMUP, Math.max(0,
                        warmupSeconds) * NANOSECONDS_PER_SECOND,
                    pollTimeoutMs, connectTimeoutMs, pending);

                // --- Settle ---
                if (settleMs > 0) {
                    PerfCommon.sleepMillis(settleMs);
                }

                // --- Active measurement ---
                pending.seq = nextSeq;
                runPublishPhase(publisher, topic, publishContext, payloadMessage,
                    payloadSegment, poller, msgSize, runId,
                    PerfMultiMetricHeader.PHASE_ACTIVE, Math.max(1,
                        durationSeconds) * NANOSECONDS_PER_SECOND,
                    pollTimeoutMs, connectTimeoutMs, pending);
            }

            return 0;
        } catch (RuntimeException ex) {
            System.err.println("ERROR,MULTI_SPOT,server,"
                + ex.getClass().getSimpleName() + ","
                + String.valueOf(ex.getMessage()));
            return 2;
        }
    }

    private static Endpoints resolveEndpoints(String transport, String name) {
        int fixedPort = PerfMultiCommon.resolveServerBindPort();
        if (fixedPort > 0) {
            return new Endpoints(
                transport + "://127.0.0.1:" + fixedPort,
                "tcp://127.0.0.1:" + (fixedPort + 1),
                "tcp://127.0.0.1:" + (fixedPort + 2)
            );
        }
        return new Endpoints(
            PerfCommon.endpointFor(transport, name),
            PerfCommon.endpointFor("tcp", name + "-registry-pub"),
            PerfCommon.endpointFor("tcp", name + "-registry-router")
        );
    }

    private static long runPublishPhase(Spot publisher,
                                        Spot.PreparedTopic topic,
                                        Spot.PublishContext publishContext,
                                        Message payloadMessage,
                                        MemorySegment payloadSegment,
                                        Poller poller,
                                        int msgSize,
                                        int runId,
                                        int phase,
                                        long durationNs,
                                        int pollTimeoutMs,
                                        int flushTimeoutMs,
                                        PendingPublish pending) {
        if (durationNs <= 0L) {
            return pending.seq;
        }
        long deadline = System.nanoTime() + durationNs;
        boolean pollOutEnabled = false;

        while (System.nanoTime() < deadline) {
            if (!pending.pending) {
                PerfMultiMetricHeader.stampPayload(payloadSegment,
                    payloadMessage.size(), runId, phase, msgSize, pending.seq,
                    PerfMultiMetricHeader.nowUs());
                pending.begin();
            }

            if (tryFlushPending(publisher, topic, publishContext, payloadMessage,
                pending)) {
                if (pollOutEnabled) {
                    poller.modifySpotPub(publisher, 0);
                    pollOutEnabled = false;
                }
                pending.seq++;
                continue;
            }

            if (!pollOutEnabled) {
                poller.modifySpotPub(publisher, PollEventType.POLLOUT.getValue());
                pollOutEnabled = true;
            }
            poller.pollCount(pollTimeoutMs);
        }

        long flushDeadline = System.nanoTime()
            + (long) Math.max(1000, flushTimeoutMs) * NANOSECONDS_PER_MILLISECOND;
        while (pending.pending) {
            if (tryFlushPending(publisher, topic, publishContext, payloadMessage,
                pending)) {
                pending.seq++;
                break;
            }
            if (!pollOutEnabled) {
                poller.modifySpotPub(publisher, PollEventType.POLLOUT.getValue());
                pollOutEnabled = true;
            }
            if (System.nanoTime() >= flushDeadline) {
                throw new IllegalStateException("spot publish stalled");
            }
            poller.pollCount(pollTimeoutMs);
        }

        if (pollOutEnabled) {
            poller.modifySpotPub(publisher, 0);
        }
        return pending.seq;
    }

    private static boolean tryFlushPending(Spot publisher,
                                           Spot.PreparedTopic topic,
                                           Spot.PublishContext publishContext,
                                           Message payloadMessage,
                                           PendingPublish pending) {
        if (!pending.pending) {
            return true;
        }
        if (!tryPublishNonBlocking(publisher, topic, publishContext,
            payloadMessage)) {
            return false;
        }
        pending.complete();
        return true;
    }

    private static boolean tryPublishNonBlocking(Spot publisher,
                                                 Spot.PreparedTopic topic,
                                                 Spot.PublishContext publishContext,
                                                 Message payloadMessage) {
        while (true) {
            try {
                publisher.publish(topic, payloadMessage, SendFlag.DONTWAIT,
                    publishContext);
                return true;
            } catch (ZlinkException ex) {
                if (isInterrupted(ex.errno())) {
                    continue;
                }
                if (isWouldBlock(ex.errno())) {
                    return false;
                }
                throw ex;
            }
        }
    }

    private static void applySpotNodeOptions(SpotNode node) {
        int sndHwm = PerfMultiCommon.resolveSndHwm(PATTERN);
        int rcvHwm = PerfMultiCommon.resolveRcvHwm(PATTERN);
        int rcvTimeoutMs = PerfMultiCommon.resolveRcvTimeoutMs();
        int xpubNoDrop = resolveXpubNoDrop();
        node.setOption(SpotNodeSocketRole.PUB, SocketOptions.SNDHWM, sndHwm);
        node.setOption(SpotNodeSocketRole.PUB, SocketOptions.RCVHWM, rcvHwm);
        node.setOption(SpotNodeSocketRole.PUB, SocketOptions.SNDTIMEO, 0);
        node.setOption(SpotNodeSocketRole.PUB, SocketOptions.RCVTIMEO,
            rcvTimeoutMs);
        node.setOption(SpotNodeSocketRole.PUB, SocketOptions.XPUB_NODROP,
            xpubNoDrop);
        node.setOption(SpotNodeSocketRole.SUB, SocketOptions.XPUB_NODROP,
            xpubNoDrop);
        node.setOption(SpotNodeSocketRole.PUB, SocketOptions.LINGER, 0);
        node.setOption(SpotNodeSocketRole.SUB, SocketOptions.LINGER, 0);
        node.setOption(SpotNodeSocketRole.DEALER, SocketOptions.LINGER, 0);
    }

    private static boolean waitPubPeers(SpotNode node,
                                        int targetClients,
                                        int timeoutMs) {
        long deadline = System.nanoTime()
            + (long) Math.max(500, timeoutMs) * 1_000_000L;
        int target = Math.max(1, targetClients);
        while (System.nanoTime() < deadline) {
            int peers;
            try {
                peers = node.pubPeers().size();
            } catch (RuntimeException ex) {
                break;
            }
            if (peers >= target) {
                return true;
            }
            PerfCommon.sleepMillis(5);
        }
        return false;
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }

    private static int resolveXpubNoDrop() {
        return PerfCommon.parsePositiveEnv("PERF_MULTI_SPOT_XPUB_NODROP", 1) > 0
            ? 1 : 0;
    }

    private record Endpoints(String serverEndpoint, String registryPub,
                             String registryRouter) {
    }
}
