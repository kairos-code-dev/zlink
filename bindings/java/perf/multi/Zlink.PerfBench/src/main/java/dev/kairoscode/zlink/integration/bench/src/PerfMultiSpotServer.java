/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.SendFlag;
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
 * SpotNode(bind) + Spot(pub) publishes stamped payloads.
 */
public final class PerfMultiSpotServer {
    private static final String PATTERN = "MULTI_SPOT";
    private static final String TOPIC = "bench";
    private static final int MIN_PAYLOAD_BYTES = 32;
    private static final long NANOSECONDS_PER_SECOND = 1_000_000_000L;

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

        try (Context context = new Context();
             SpotNode pubNode = new SpotNode(context);
             Spot publisher = new Spot(pubNode);
             Spot.PreparedTopic topic = publisher.prepareTopic(TOPIC);
             Spot.PublishContext publishContext =
                 publisher.createPublishContext();
             Message reusableMessage = new Message(payloadSize)) {
            PerfCommon.applyServerContextOptions(context);
            applySpotNodeOptions(pubNode);
            PerfMultiTls.configureSpotPublisherTlsIfNeeded(pubNode, transport);
            pubNode.bind(endpoint);
            System.out.println("READY," + endpoint);
            waitPubPeers(pubNode, clients, connectTimeoutMs);

            byte[] payload = new byte[payloadSize];
            MemorySegment payloadSegment = MemorySegment.ofArray(payload);
            int runId = (int) (PerfMultiMetricHeader.nowUs() & 0x7FFF_FFFFL);
            long seq = 1;

            // --- Warmup ---
            long warmupDeadline = System.nanoTime()
                + (long) Math.max(0, warmupSeconds) * NANOSECONDS_PER_SECOND;
            while (System.nanoTime() < warmupDeadline) {
                PerfMultiMetricHeader.stampPayload(payload, runId,
                    PerfMultiMetricHeader.PHASE_WARMUP, msgSize, seq++,
                    PerfMultiMetricHeader.nowUs());
                publishOnce(publisher, topic, publishContext, payloadSegment,
                    reusableMessage);
            }

            // --- Settle ---
            if (settleMs > 0) {
                PerfCommon.sleepMillis(settleMs);
            }

            // --- Active measurement ---
            long activeDeadline = System.nanoTime()
                + (long) Math.max(1, durationSeconds) * NANOSECONDS_PER_SECOND;
            while (System.nanoTime() < activeDeadline) {
                PerfMultiMetricHeader.stampPayload(payload, runId,
                    PerfMultiMetricHeader.PHASE_ACTIVE, msgSize, seq++,
                    PerfMultiMetricHeader.nowUs());
                publishOnce(publisher, topic, publishContext, payloadSegment,
                    reusableMessage);
            }

            return 0;
        } catch (RuntimeException ignored) {
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

    private static void publishOnce(Spot publisher, Spot.PreparedTopic topic,
                                    Spot.PublishContext context,
                                    MemorySegment payloadSegment,
                                    Message reusableMessage) {
        MemorySegment.copy(payloadSegment, 0, reusableMessage.dataSegment(),
            0, payloadSegment.byteSize());
        publisher.publish(topic, reusableMessage, SendFlag.NONE, context);
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

    private static void waitPubPeers(SpotNode node,
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
                return;
            }
            PerfCommon.sleepMillis(5);
        }
    }
}
