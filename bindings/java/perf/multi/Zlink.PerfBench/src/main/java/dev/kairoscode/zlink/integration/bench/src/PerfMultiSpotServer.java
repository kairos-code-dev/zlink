/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.integration.bench.common.PerfCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiClientHelpers;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiMetricHeader;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiTls;
import dev.kairoscode.zlink.options.SocketOptions;
import dev.kairoscode.zlink.service.spot.SpotNode;
import dev.kairoscode.zlink.service.spot.SpotNodeSocketRole;
import java.nio.charset.StandardCharsets;

/**
 * MULTI_SPOT server benchmark.
 * SpotNode(bind) + pollable pub socket sends topic+payload multipart frames.
 */
public final class PerfMultiSpotServer {
    private static final String PATTERN = "MULTI_SPOT";
    private static final String TOPIC = "bench";
    private static final byte[] TOPIC_BYTES = TOPIC.getBytes(
        StandardCharsets.UTF_8);
    private static final int MIN_PAYLOAD_BYTES = 32;
    private static final long NANOSECONDS_PER_SECOND = 1_000_000_000L;
    private static final long NANOSECONDS_PER_MILLISECOND = 1_000_000L;
    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

    private static final class PendingPublish {
        boolean sendTopic;
        boolean sendPayload;
        long seq;

        PendingPublish(long seq) {
            this.seq = seq;
            this.sendTopic = false;
            this.sendPayload = false;
        }

        boolean hasPending() {
            return sendTopic || sendPayload;
        }

        void begin() {
            sendTopic = true;
            sendPayload = true;
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
             SpotNode pubNode = new SpotNode(context)) {
            PerfCommon.applyServerContextOptions(context);
            applySpotNodeOptions(pubNode);
            PerfMultiTls.configureSpotPublisherTlsIfNeeded(pubNode, transport);
            pubNode.bind(endpoint);
            System.out.println("READY," + endpoint);
            if (!waitPubPeers(pubNode, clients, connectTimeoutMs)) {
                System.err.println("ERROR,MULTI_SPOT,server,no_pub_peers");
                return 2;
            }

            try (Socket publisher = pubNode.pubSocket();
                 Poller poller = new Poller()) {
                byte[] payload = new byte[payloadSize];
                int runId = (int) (PerfMultiMetricHeader.nowUs()
                    & 0x7FFF_FFFFL);
                PendingPublish pending = new PendingPublish(1L);

                // --- Warmup ---
                runPublishPhase(publisher, poller, payload, msgSize, runId,
                    PerfMultiMetricHeader.PHASE_WARMUP, Math.max(0,
                        warmupSeconds) * NANOSECONDS_PER_SECOND, pollTimeoutMs,
                    connectTimeoutMs, pending);

                // --- Settle ---
                if (settleMs > 0) {
                    PerfCommon.sleepMillis(settleMs);
                }

                // --- Active measurement ---
                runPublishPhase(publisher, poller, payload, msgSize, runId,
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

    private static void runPublishPhase(Socket socket, Poller poller,
                                        byte[] payload, int msgSize, int runId,
                                        int phase, long durationNs,
                                        int pollTimeoutMs, int flushTimeoutMs,
                                        PendingPublish pending) {
        if (durationNs <= 0L) {
            return;
        }
        long deadline = System.nanoTime() + durationNs;
        boolean pollOutRegistered = false;

        while (System.nanoTime() < deadline) {
            if (!pending.hasPending()) {
                PerfMultiMetricHeader.stampPayload(payload, runId, phase,
                    msgSize, pending.seq++, PerfMultiMetricHeader.nowUs());
                pending.begin();
            }

            if (tryFlushPending(socket, payload, pending)) {
                if (pollOutRegistered) {
                    poller.remove(socket);
                    pollOutRegistered = false;
                }
                continue;
            }

            if (!pollOutRegistered) {
                poller.add(socket, PollEventType.POLLOUT);
                pollOutRegistered = true;
            }
            poller.pollCount(pollTimeoutMs);
        }

        long flushDeadline = System.nanoTime()
            + (long) Math.max(1000, flushTimeoutMs) * NANOSECONDS_PER_MILLISECOND;
        while (pending.hasPending()) {
            if (tryFlushPending(socket, payload, pending)) {
                break;
            }
            if (!pollOutRegistered) {
                poller.add(socket, PollEventType.POLLOUT);
                pollOutRegistered = true;
            }
            if (System.nanoTime() >= flushDeadline) {
                throw new IllegalStateException("spot publish stalled");
            }
            poller.pollCount(pollTimeoutMs);
        }

        if (pollOutRegistered) {
            poller.remove(socket);
        }
    }

    private static boolean tryFlushPending(Socket socket, byte[] payload,
                                           PendingPublish pending) {
        if (pending.sendTopic) {
            if (!trySendNonBlocking(socket, TOPIC_BYTES,
                SendFlag.DONTWAIT_SNDMORE)) {
                return false;
            }
            pending.sendTopic = false;
        }
        if (pending.sendPayload) {
            if (!trySendNonBlocking(socket, payload, SendFlag.DONTWAIT)) {
                return false;
            }
            pending.sendPayload = false;
        }
        return true;
    }

    private static boolean trySendNonBlocking(Socket socket, byte[] payload,
                                              SendFlag flags) {
        while (true) {
            try {
                int sent = socket.send(payload, 0, payload.length, flags);
                if (sent != payload.length) {
                    throw new IllegalStateException(
                        "partial send: " + sent + "/" + payload.length);
                }
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
        node.setOption(SpotNodeSocketRole.PUB, SocketOptions.SNDTIMEO,
            0);
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
