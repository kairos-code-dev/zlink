/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
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
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.atomic.LongAdder;

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

            connectRegistryWithRetry(() -> discovery.connectRegistry(regRouter));
            discovery.subscribe(serviceName);

            applyServiceSocketOptions(gateway, receiver);

            String providerEndpoint = PerfCommon.endpointFor(tr,
                "gateway-provider");
            PerfTls.configureReceiverTlsServerIfNeeded(receiver, tr);
            receiver.bind(providerEndpoint);
            connectRegistryWithRetry(() -> receiver.connectRegistry(regRouter));
            receiver.register(serviceName, providerEndpoint, 1);

            PerfTls.configureGatewayTlsClientIfNeeded(gateway, tr);
            int readyTimeoutMs = PerfCommon.resolveGatewayReadyTimeoutMs();

            if (!PerfCommon.waitUntil(
                () -> receiver.registerResult(serviceName).status() == 0,
                readyTimeoutMs, 1)) {
                return 2;
            }

            if (!PerfCommon.waitUntil(() -> discovery.receiverCount(serviceName) > 0,
                readyTimeoutMs, 1)) {
                return 2;
            }

            if (!PerfCommon.waitUntil(() -> gateway.connectionCount(serviceName) > 0,
                readyTimeoutMs, 1)) {
                return 2;
            }

            int payloadSize = Math.max(msgSize, PerfSingleMetricHeader.HEADER_SIZE);
            int warmupCount = PerfCommon.resolveWarmupCount(PATTERN, msgSize);
            int durationSeconds = PerfCommon.resolveDurationSeconds();
            int latencyCap = PerfCommon.resolveLatencySampleCap();
            int recvTimeoutMs = PerfCommon.resolveRecvTimeoutMs();
            int runId = PerfCommon.randomRunId();

            try (Arena arena = Arena.ofConfined()) {
                MemorySegment payload = arena.allocate(payloadSize, 8);
                payload.fill((byte) 'a');
                long[] seq = new long[] {1L};

                PhaseResult warmup = runPhase(gateway, receiver, serviceName,
                    payload, payloadSize, msgSize, runId, seq,
                    PerfSingleMetricHeader.PHASE_WARMUP, warmupCount, 0, 0,
                    recvTimeoutMs);
                if (!warmup.ok || warmup.received < warmupCount) {
                    return 2;
                }

                Thread.sleep(PerfCommon.resolveSettleMs());

                PhaseResult active = runPhase(gateway, receiver, serviceName,
                    payload, payloadSize, msgSize, runId, seq,
                    PerfSingleMetricHeader.PHASE_ACTIVE, 0, durationSeconds,
                    latencyCap, recvTimeoutMs);
                if (!active.ok || active.received <= 0) {
                    return 2;
                }

                double throughput =
                    active.received / (double) Math.max(durationSeconds, 1);
                PerfCommon.Stats stats = active.latency.snapshot();
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

    private static PhaseResult runPhase(Gateway gateway, Receiver receiver,
                                        String serviceName,
                                        MemorySegment payload, int payloadSize,
                                        int msgSize, int runId, long[] seq,
                                        int phase, int warmupCount,
                                        int durationSeconds, int latencyCap,
                                        int recvTimeoutMs) {
        final boolean active = durationSeconds > 0;
        final long deadlineNs = active
            ? System.nanoTime()
                + (long) Math.max(durationSeconds, 1) * 1_000_000_000L
            : 0L;
        final long drainDeadlineNs =
            (long) Math.max(recvTimeoutMs, 1) * 1_000_000L;
        final LongAdder received = new LongAdder();
        final AtomicBoolean senderDone = new AtomicBoolean(false);
        final AtomicReference<RuntimeException> recvError =
            new AtomicReference<>();
        final PerfCommon.LatencyReservoir latency =
            new PerfCommon.LatencyReservoir(Math.max(latencyCap, 1));

        Thread receiverThread = new Thread(() -> {
            long lastRecvNs = System.nanoTime();
            PerfSingleMetricHeader.Header header =
                new PerfSingleMetricHeader.Header();

            try (Arena recvArena = Arena.ofConfined()) {
                MemorySegment recvBuffer = recvArena.allocate(payloadSize, 8);
                while (true) {
                    boolean done = senderDone.get();
                    try {
                        int bytesRead = receiver.recvSinglePayload(recvBuffer,
                            done ? ReceiveFlag.DONTWAIT : ReceiveFlag.NONE);
                        lastRecvNs = System.nanoTime();
                        accountMessage(recvBuffer, bytesRead, payloadSize,
                            active, runId, phase, header, received, latency);

                        while (true) {
                            bytesRead = receiver.recvSinglePayload(recvBuffer,
                                ReceiveFlag.DONTWAIT);
                            lastRecvNs = System.nanoTime();
                            accountMessage(recvBuffer, bytesRead, payloadSize,
                                active, runId, phase, header, received, latency);
                        }
                    } catch (ZlinkException ex) {
                        if (isInterrupted(ex.errno())) {
                            continue;
                        }
                        if (isWouldBlock(ex.errno())) {
                            if (done
                                && System.nanoTime() - lastRecvNs >= drainDeadlineNs) {
                                break;
                            }
                            Thread.yield();
                            continue;
                        }
                        throw ex;
                    }
                }
            } catch (RuntimeException ex) {
                recvError.set(ex);
            }
        }, "zlink-java-perf-gateway-recv");
        receiverThread.setDaemon(true);
        receiverThread.start();

        boolean ok = true;
        if (active) {
            while (System.nanoTime() < deadlineNs) {
                if (!PerfSingleMetricHeader.stampPayload(payload, payloadSize,
                    runId, phase, msgSize, seq[0]++,
                    PerfSingleMetricHeader.nowUs())) {
                    ok = false;
                    break;
                }
                try {
                    gateway.sendTo(serviceName, payload, SendFlag.NONE);
                } catch (RuntimeException ex) {
                    ok = false;
                    break;
                }
            }
        } else {
            for (int i = 0; i < warmupCount; i++) {
                if (!PerfSingleMetricHeader.stampPayload(payload, payloadSize,
                    runId, phase, msgSize, seq[0]++,
                    PerfSingleMetricHeader.nowUs())) {
                    ok = false;
                    break;
                }
                try {
                    gateway.sendTo(serviceName, payload, SendFlag.NONE);
                } catch (RuntimeException ex) {
                    ok = false;
                    break;
                }
            }
        }

        senderDone.set(true);
        try {
            receiverThread.join();
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            return new PhaseResult(false, received.sum(), latency);
        }

        return new PhaseResult(ok && recvError.get() == null, received.sum(),
            latency);
    }

    private static void accountMessage(MemorySegment recvBuffer, int bytesRead,
                                       int payloadSize, boolean active,
                                       int runId, int phase,
                                       PerfSingleMetricHeader.Header header,
                                       LongAdder received,
                                       PerfCommon.LatencyReservoir latency) {
        if (bytesRead != payloadSize) {
            return;
        }
        if (!PerfSingleMetricHeader.decodePayloadHeader(recvBuffer, payloadSize,
            header)) {
            return;
        }
        if (header.runId != runId || header.phase != phase) {
            return;
        }

        received.increment();
        if (!active) {
            return;
        }
        long nowUs = PerfSingleMetricHeader.nowUs();
        latency.add(Math.max(0L, nowUs - header.sentTsUs));
    }

    private static void applyServiceSocketOptions(Gateway gateway,
                                                  Receiver receiver) {
        int sndHwm = resolveSingleHwmValue("PERF_SINGLE_SNDHWM");
        int rcvHwm = resolveSingleHwmValue("PERF_SINGLE_RCVHWM");
        int sndTimeo = PerfCommon.parseEnvNonNegative(
            "PERF_SINGLE_SNDTIMEO_MS", 200);
        int recvTimeo = PerfCommon.parseEnvNonNegative(
            "PERF_SINGLE_RCVTIMEO_MS", 200);

        gateway.setOption(SocketOptions.SNDHWM, sndHwm);
        gateway.setOption(SocketOptions.RCVHWM, rcvHwm);
        gateway.setOption(SocketOptions.SNDTIMEO, sndTimeo);
        gateway.setOption(SocketOptions.RCVTIMEO, recvTimeo);

        receiver.setOption(SocketOptions.SNDHWM, sndHwm);
        receiver.setOption(SocketOptions.RCVHWM, rcvHwm);
        receiver.setOption(SocketOptions.SNDTIMEO, sndTimeo);
        receiver.setOption(SocketOptions.RCVTIMEO, recvTimeo);
    }

    private static int resolveSingleHwmValue(String specificName) {
        int hwm = PerfCommon.parseEnv("PERF_SINGLE_HWM", 1000);
        int specific = PerfCommon.parseEnv(specificName, 0);
        return specific > 0 ? specific : hwm;
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

    private record PhaseResult(boolean ok, long received,
                               PerfCommon.LatencyReservoir latency) {
    }
}
