/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.integration.bench.common.PerfCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfSingleMetricHeader;
import dev.kairoscode.zlink.integration.bench.common.PerfTls;
import dev.kairoscode.zlink.options.SocketOptions;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.gateway.Gateway;
import dev.kairoscode.zlink.service.receiver.Receiver;
import dev.kairoscode.zlink.service.registry.Registry;
import java.lang.foreign.MemorySegment;

/**
 * GATEWAY benchmark using high-level Gateway/Receiver APIs.
 */
public final class PerfGateway {
    private static final String PATTERN = "GATEWAY";

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

    private PerfGateway() {
    }

    public static int run(String transport, int msgSize) {
        String tr = transport == null ? "" : transport.toLowerCase();
        if ("inproc".equals(tr) || "ipc".equals(tr)) {
            PerfCommon.printUnsupported(PATTERN, tr, msgSize,
                "gateway does not support inproc/ipc");
            return 0;
        }

        String unique = Long.toHexString(System.nanoTime());
        String regPub = PerfCommon.endpointFor("tcp", "java-perf-gw-pub-" + unique);
        String regRouter = PerfCommon.endpointFor("tcp",
            "java-perf-gw-router-" + unique);
        String serviceName = "svc";

        try (Context context = new Context();
             Registry registry = new Registry(context);
             Discovery discovery = new Discovery(context, ServiceType.GATEWAY);
             Receiver receiver = new Receiver(context);
             Gateway gateway = new Gateway(context, discovery)) {
            PerfCommon.applySingleContextOptions(context);

            registry.setEndpoints(regPub, regRouter);
            registry.setHeartbeat(5000, 60000);
            registry.start();

            connectRegistryWithRetry(() -> discovery.connectRegistry(regRouter));
            discovery.subscribe(serviceName);

            String providerEndpoint = PerfCommon.endpointFor(tr,
                "gateway-provider");
            PerfTls.configureReceiverTlsServerIfNeeded(receiver, tr);
            receiver.bind(providerEndpoint);
            connectRegistryWithRetry(() -> receiver.connectRegistry(regRouter));
            receiver.register(serviceName, providerEndpoint, 1);

            PerfTls.configureGatewayTlsClientIfNeeded(gateway, tr);

            if (!PerfCommon.waitUntil(
                () -> receiver.registerResult(serviceName).status() == 0,
                5000,
                20)) {
                return 2;
            }

            if (!PerfCommon.waitUntil(() -> discovery.receiverCount(serviceName) > 0,
                5000,
                20)) {
                return 2;
            }

            if (!PerfCommon.waitUntil(() -> gateway.connectionCount(serviceName) > 0,
                5000,
                20)) {
                return 2;
            }

            receiver.setOption(SocketOptions.RCVTIMEO, 5000);

            int payloadSize = Math.max(msgSize,
                PerfSingleMetricHeader.HEADER_SIZE);
            byte[] payload = new byte[payloadSize];
            byte[] recv = new byte[payloadSize];

            int runId = PerfCommon.randomRunId();
            long seq = 1;
            try (Message payloadMessage = new Message(payloadSize)) {

                MemorySegment payloadSegment = MemorySegment.ofArray(payload);
                int warmupCount = PerfCommon.resolveWarmupCount(PATTERN, msgSize);
                for (int i = 0; i < warmupCount; i++) {
                    PerfSingleMetricHeader.stampPayload(payload, runId,
                        PerfSingleMetricHeader.PHASE_WARMUP, msgSize, seq++,
                        PerfSingleMetricHeader.nowUs());
                    MemorySegment.copy(payloadSegment, 0,
                        payloadMessage.dataSegment(), 0, payloadSize);
                    sendGatewayWithRetry(gateway, serviceName, payloadMessage,
                        5000);
                    receiveProviderPayload(receiver, recv, 5000);
                }

                Thread.sleep(PerfCommon.resolveSettleMs());

                PerfSingleMetricHeader.Header header =
                    new PerfSingleMetricHeader.Header();
                PerfCommon.LatencyReservoir latency =
                    new PerfCommon.LatencyReservoir(
                        PerfCommon.resolveLatencySampleCap());

                long received = 0;
                long endNs = System.nanoTime()
                    + (long) PerfCommon.resolveDurationSeconds() * 1_000_000_000L;
                long beginNs = System.nanoTime();

                while (System.nanoTime() < endNs) {
                    PerfSingleMetricHeader.stampPayload(payload, runId,
                        PerfSingleMetricHeader.PHASE_ACTIVE, msgSize, seq++,
                        PerfSingleMetricHeader.nowUs());
                    MemorySegment.copy(payloadSegment, 0,
                        payloadMessage.dataSegment(), 0, payloadSize);
                    sendGatewayWithRetry(gateway, serviceName, payloadMessage,
                        5000);

                    receiveProviderPayload(receiver, recv, 5000);
                    if (PerfSingleMetricHeader.decodePayloadHeader(recv, header)
                        && header.runId == runId
                        && header.phase == PerfSingleMetricHeader.PHASE_ACTIVE) {
                        long nowUs = PerfSingleMetricHeader.nowUs();
                        latency.add(Math.max(0L, nowUs - header.sentTsUs));
                        received++;
                    }
                }

                double elapsedSec = Math.max(1e-9,
                    (System.nanoTime() - beginNs) / 1_000_000_000.0);
                double throughput = received / elapsedSec;
                PerfCommon.Stats stats = latency.snapshot();
                PerfCommon.printResult(PATTERN, tr, msgSize, throughput,
                    stats.meanUs(), stats.p95Us(), stats.p99Us());
                return 0;
            }
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            return 2;
        } catch (RuntimeException ex) {
            String message = ex.getMessage() == null
                ? ex.getClass().getSimpleName()
                : ex.getMessage();
            System.err.println("single_gateway_error:" + message);
            return 2;
        }
    }

    private static void receiveProviderPayload(Receiver receiver,
                                               byte[] payloadBuffer,
                                               int timeoutMs) {
        long deadlineNs = System.nanoTime()
            + (long) Math.max(timeoutMs, 1) * 1_000_000L;
        RuntimeException last = null;
        while (System.nanoTime() < deadlineNs) {
            try (Receiver.ReceiverMessages received = receiver.recv(
                ReceiveFlag.DONTWAIT)) {
                Message[] parts = received.parts();
                if (parts.length == 0) {
                    throw new IllegalStateException("gateway provider payload missing");
                }
                Message payload = parts[parts.length - 1];
                payload.copyTo(payloadBuffer);
                return;
            } catch (ZlinkException ex) {
                if (isInterrupted(ex.errno())) {
                    continue;
                }
                if (isWouldBlock(ex.errno())) {
                    last = ex;
                    continue;
                }
                throw ex;
            }
        }
        if (last != null) {
            throw last;
        }
        throw new IllegalStateException("gateway provider payload receive timed out");
    }

    private static void sendGatewayWithRetry(Gateway gateway,
                                             String serviceName,
                                             Message payload,
                                             int timeoutMs) {
        long deadlineNs = System.nanoTime()
            + (long) Math.max(timeoutMs, 1) * 1_000_000L;
        RuntimeException last = null;
        while (System.nanoTime() < deadlineNs) {
            try {
                gateway.sendTo(serviceName, payload, SendFlag.DONTWAIT);
                return;
            } catch (ZlinkException ex) {
                if (isInterrupted(ex.errno())) {
                    continue;
                }
                if (isWouldBlock(ex.errno())) {
                    last = ex;
                    continue;
                }
                throw ex;
            }
        }
        if (last != null) {
            throw last;
        }
        throw new IllegalStateException("gateway send timed out");
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }

    private static void connectRegistryWithRetry(Runnable connect) {
        RuntimeException last = null;
        long deadlineNs = System.nanoTime() + 5_000_000_000L;
        while (System.nanoTime() < deadlineNs) {
            try {
                connect.run();
                return;
            } catch (ZlinkException ex) {
                if (!isInterrupted(ex.errno()) && !isWouldBlock(ex.errno())) {
                    throw ex;
                }
                last = ex;
            }
            try {
                Thread.sleep(20);
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("registry connect interrupted", ex);
            }
        }

        if (last != null) {
            throw last;
        }
        throw new IllegalStateException("registry connect timeout");
    }
}
