/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.Socket;
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
 * sender=Gateway, receiver=Receiver.routerSocket().
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
        String regPub = "inproc://java-perf-gw-pub-" + unique;
        String regRouter = "inproc://java-perf-gw-router-" + unique;
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

            discovery.connectRegistry(regPub);
            discovery.subscribe(serviceName);

            String providerEndpoint = PerfCommon.endpointFor(tr,
                "gateway-provider");
            PerfTls.configureReceiverTlsServerIfNeeded(receiver, tr);
            receiver.bind(providerEndpoint);
            receiver.connectRegistry(regRouter);
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

            try (Socket providerRouter = receiver.routerSocket()) {
                PerfCommon.applySingleSocketOptions(providerRouter);
                providerRouter.setOption(SocketOptions.RCVTIMEO, 5000);

                int payloadSize = Math.max(msgSize,
                    PerfSingleMetricHeader.HEADER_SIZE);
                byte[] payload = new byte[payloadSize];
                byte[] recv = new byte[payloadSize];
                byte[] routingId = new byte[256];

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
                        gateway.sendTo(serviceName, payloadMessage);
                        gatewayReceiveProviderMessage(providerRouter, routingId, recv);
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
                        gateway.sendTo(serviceName, payloadMessage);

                        gatewayReceiveProviderMessage(providerRouter, routingId, recv);
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
            }
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            return 2;
        } catch (RuntimeException ex) {
            return 2;
        }
    }

    private static void gatewayReceiveProviderMessage(Socket router,
                                                      byte[] routingIdBuffer,
                                                      byte[] payloadBuffer) {
        int idLen = receiveBlocking(router, routingIdBuffer);
        if (idLen <= 0 || getRcvMore(router) == 0) {
            throw new IllegalStateException(
                "gateway provider message missing routing frame");
        }

        int payloadLen = receiveBlocking(router, payloadBuffer);
        if (payloadLen < 0) {
            throw new IllegalStateException(
                "gateway provider message payload receive failed");
        }

        while (getRcvMore(router) != 0) {
            int drained = receiveNonBlocking(router, payloadBuffer);
            if (drained <= 0) {
                throw new IllegalStateException(
                    "gateway provider message multipart drain failed");
            }
        }
    }

    private static int receiveBlocking(Socket socket, byte[] buffer) {
        while (true) {
            try {
                return socket.recv(buffer, 0, buffer.length, ReceiveFlag.NONE);
            } catch (ZlinkException ex) {
                if (isInterrupted(ex.errno())) {
                    continue;
                }
                if (isWouldBlock(ex.errno())) {
                    return 0;
                }
                throw ex;
            }
        }
    }

    private static int receiveNonBlocking(Socket socket, byte[] buffer) {
        while (true) {
            try {
                return socket.recv(buffer, 0, buffer.length, ReceiveFlag.DONTWAIT);
            } catch (ZlinkException ex) {
                if (isInterrupted(ex.errno())) {
                    continue;
                }
                if (isWouldBlock(ex.errno())) {
                    return 0;
                }
                throw ex;
            }
        }
    }

    private static int getRcvMore(Socket socket) {
        Integer value = socket.getOption(SocketOptions.RCVMORE);
        return value == null ? 0 : value;
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }
}
