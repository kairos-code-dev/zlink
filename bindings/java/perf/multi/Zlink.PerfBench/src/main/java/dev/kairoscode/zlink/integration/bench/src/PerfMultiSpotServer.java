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
import dev.kairoscode.zlink.options.SocketOptions;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import dev.kairoscode.zlink.service.spot.SpotNodeSocketRole;
import java.lang.foreign.MemorySegment;

/**
 * MULTI_SPOT server benchmark.
 * Spot service instance publishes payload, poller registers SpotPub directly.
 */
public final class PerfMultiSpotServer {
    private static final String PATTERN = "MULTI_SPOT";
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

        String endpoint = resolveServerEndpoint(transport, "multi-spot");
        int payloadSize = Math.max(msgSize, MIN_PAYLOAD_BYTES);
        int clients = PerfMultiCommon.resolveClients(PATTERN);
        int connectTimeoutMs = PerfMultiCommon.resolveConnectReadyTimeoutMs();
        int warmupSeconds = PerfMultiCommon.resolveWarmupSeconds();
        int durationSeconds = PerfMultiCommon.resolveDurationSeconds();
        int settleMs = PerfMultiCommon.resolveSettleMs();
        int pollTimeoutMs = Math.max(1,
            PerfMultiCommon.resolveClientPollTimeoutMs());

        try (Context context = new Context();
             SpotNode node = new SpotNode(context)) {
            PerfCommon.applyServerContextOptions(context);
            applySpotNodeOptions(node);
            PerfMultiTls.configureSpotPublisherTlsIfNeeded(node, transport);
            node.bind(endpoint);
            System.out.println("READY," + endpoint);
            if (!waitPubPeers(node, clients, connectTimeoutMs)) {
                System.err.println("ERROR,MULTI_SPOT,server,no_pub_peers");
                return 2;
            }

            try (Spot publisher = new Spot(node);
                 Spot.PreparedTopic topic = publisher.prepareTopic(TOPIC);
                 Spot.PublishContext publishContext =
                     publisher.createPublishContext();
                 Message payloadMessage = new Message(payloadSize);
                 Poller poller = new Poller()) {
                byte[] payload = new byte[payloadSize];
                MemorySegment payloadSegment = MemorySegment.ofArray(payload);
                int runId = (int) (PerfMultiMetricHeader.nowUs()
                    & 0x7FFF_FFFFL);
                PendingPublish pending = new PendingPublish(1L);

                // --- Warmup ---
                runPublishPhase(publisher, topic, publishContext, payloadMessage,
                    poller, payload, payloadSegment, msgSize, runId,
                    PerfMultiMetricHeader.PHASE_WARMUP, Math.max(0,
                        warmupSeconds) * NANOSECONDS_PER_SECOND, pollTimeoutMs,
                    connectTimeoutMs, pending);

                // --- Settle ---
                if (settleMs > 0) {
                    PerfCommon.sleepMillis(settleMs);
                }

                // --- Active measurement ---
                runPublishPhase(publisher, topic, publishContext, payloadMessage,
                    poller, payload, payloadSegment, msgSize, runId,
                    PerfMultiMetricHeader.PHASE_ACTIVE, Math.max(1,
                        durationSeconds) * NANOSECONDS_PER_SECOND, pollTimeoutMs,
                    connectTimeoutMs, pending);
            }

            return 0;
        } catch (RuntimeException ex) {
            System.err.println("ERROR,MULTI_SPOT,server,"
                + ex.getClass().getSimpleName() + ","
                + String.valueOf(ex.getMessage()));
            return 2;
        }
    }

    private static String resolveServerEndpoint(String transport, String name) {
        int fixedPort = PerfMultiCommon.resolveServerBindPort();
        if (fixedPort > 0) {
            return transport + "://127.0.0.1:" + fixedPort;
        }
        return PerfCommon.endpointFor(transport, name);
    }

    private static void runPublishPhase(Spot publisher,
                                        Spot.PreparedTopic topic,
                                        Spot.PublishContext publishContext,
                                        Message payloadMessage,
                                        Poller poller,
                                        byte[] payload,
                                        MemorySegment payloadSegment,
                                        int msgSize,
                                        int runId,
                                        int phase,
                                        long durationNs,
                                        int pollTimeoutMs,
                                        int flushTimeoutMs,
                                        PendingPublish pending) {
        if (durationNs <= 0L) {
            return;
        }
        long deadline = System.nanoTime() + durationNs;
        boolean pollOutRegistered = false;

        while (System.nanoTime() < deadline) {
            if (!pending.pending) {
                PerfMultiMetricHeader.stampPayload(payload, runId, phase,
                    msgSize, pending.seq++, PerfMultiMetricHeader.nowUs());
                MemorySegment.copy(payloadSegment, 0, payloadMessage.dataSegment(),
                    0, payload.length);
                pending.begin();
            }

            if (tryFlushPending(publisher, topic, publishContext,
                payloadMessage, pending)) {
                if (pollOutRegistered) {
                    poller.removeSpotPub(publisher);
                    pollOutRegistered = false;
                }
                continue;
            }

            if (!pollOutRegistered) {
                poller.addSpotPub(publisher, PollEventType.POLLOUT);
                pollOutRegistered = true;
            }
            poller.pollCount(pollTimeoutMs);
        }

        long flushDeadline = System.nanoTime()
            + (long) Math.max(1000, flushTimeoutMs) * NANOSECONDS_PER_MILLISECOND;
        while (pending.pending) {
            if (tryFlushPending(publisher, topic, publishContext,
                payloadMessage, pending)) {
                break;
            }
            if (!pollOutRegistered) {
                poller.addSpotPub(publisher, PollEventType.POLLOUT);
                pollOutRegistered = true;
            }
            if (System.nanoTime() >= flushDeadline) {
                throw new IllegalStateException("spot publish stalled");
            }
            poller.pollCount(pollTimeoutMs);
        }

        if (pollOutRegistered) {
            poller.removeSpotPub(publisher);
        }
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
                                                 Spot.PublishContext context,
                                                 Message payloadMessage) {
        while (true) {
            try {
                publisher.publish(topic, payloadMessage, SendFlag.DONTWAIT,
                    context);
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
        node.setOption(SpotNodeSocketRole.PUB, SocketOptions.SNDHWM, sndHwm);
        node.setOption(SpotNodeSocketRole.PUB, SocketOptions.SNDTIMEO, 0);
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
}
