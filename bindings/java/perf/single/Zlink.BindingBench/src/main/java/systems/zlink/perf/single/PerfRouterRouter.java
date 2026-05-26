/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.single;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.Message;
import systems.zlink.contracts.MonitorEventType;
import systems.zlink.contracts.PollEventFlag;
import systems.zlink.contracts.RecvException;
import systems.zlink.contracts.RecvFlags;
import systems.zlink.contracts.RecvResult;
import systems.zlink.contracts.RouterSocket;
import systems.zlink.contracts.RoutingId;
import systems.zlink.contracts.SendFlags;
import systems.zlink.contracts.SocketType;
import systems.zlink.perf.PerfSocketPollSet;
import systems.zlink.perf.PerfStopToken;
import systems.zlink.perf.PerfUtil;
import java.util.Arrays;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

final class PerfRouterRouter {
    private static final MonitorEventType READY_EVENT = MonitorEventType.CONNECTION_READY;
    private static final RoutingId ROUTER1 = RoutingId.fromBytes(
        "ROUTER1".getBytes(StandardCharsets.UTF_8));
    private static final RoutingId ROUTER2 = RoutingId.fromBytes(
        "ROUTER2".getBytes(StandardCharsets.UTF_8));
    private static final byte[] PING = "PING".getBytes(StandardCharsets.UTF_8);
    private static final byte[] PONG = "PONG".getBytes(StandardCharsets.UTF_8);

    private PerfRouterRouter() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String endpoint = PerfUtil.endpoint(config.transport(), "single-router-router");
        CountDownLatch finished = new CountDownLatch(1);
        CountDownLatch routed = new CountDownLatch(1);
        AtomicBoolean probePending = new AtomicBoolean(true);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        boolean sharedContext = true;
        Context receiverCtx = PerfUtil.newContext(config);
        Context senderCtx = sharedContext ? receiverCtx : PerfUtil.newContext(config);
        try (RouterSocket receiver = new RouterSocket(receiverCtx);
             RouterSocket sender = new RouterSocket(senderCtx);
             var receiverMonitor = receiver.monitorOpen(MonitorEventType.CONNECTION_READY);
             var senderMonitor = sender.monitorOpen(MonitorEventType.CONNECTION_READY)) {
            Duration readyTimeout = Duration.ofMillis(config.connectReadyTimeoutMs());
            PerfUtil.applyMonitorOptions(receiverMonitor, config);
            PerfUtil.applyMonitorOptions(senderMonitor, config);
            PerfUtil.applySocketOptions(receiver, config);
            PerfUtil.applySocketOptions(sender, config);
            PerfUtil.recalculateAutoHwm(receiverCtx);
            PerfUtil.configureServerTls(receiver, config.transport());
            PerfUtil.configureClientTls(sender, config.transport());
            receiver.setRoutingId(ROUTER1);
            sender.setRoutingId(ROUTER2);
            receiver.options().mandatory(true);
            sender.options().mandatory(true);
            sender.options().connectRoutingId(ROUTER1);
            receiver.bind(PerfUtil.bindEndpoint(endpoint, config.transport()));
            sender.connect(PerfUtil.connectedEndpoint(receiver, endpoint,
                config.transport()));
            PerfUtil.waitForMonitorEvent(senderMonitor, READY_EVENT, 1,
                readyTimeout, "router/router sender ready");
            PerfUtil.waitForMonitorEvent(receiverMonitor, READY_EVENT, 1,
                readyTimeout, "router/router receiver ready");
            RoutingId targetRoute = performRouterRouterHandshake(receiver, sender,
                Duration.ofMillis(config.connectReadyTimeoutMs()));
            drainRouterSocket(receiver);
            drainRouterSocket(sender);

            // PERF_SINGLE_TEST_POLICY § 1.4: receiver waits with -1 and exits
            // on wire-level stop token.
            long activeEnd = System.nanoTime()
                + config.durationSeconds() * 1_000_000_000L;
            Thread receiverThread = new Thread(() -> {
                try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                    List.of(receiver), PollEventFlag.POLLIN)) {
                    while (true) {
                        pollSet.poll(-1);
                        boolean stop = false;
                        while (true) {
                            systems.zlink.contracts.Received received =
                                PerfUtil.recvNoWait(receiver);
                            if (received == null) {
                                break;
                            }
                            try (received) {
                                if (PerfStopToken.isStopTokenMessage(received.firstPart())) {
                                    stop = true;
                                    break;
                                }
                                PerfUtil.Header header = PerfUtil.decodeHeader(
                                    received.firstPart(), config.size());
                                if (header == null) {
                                    continue;
                                }
                                if (header.phase() == PerfUtil.PHASE_WARMUP
                                    && probePending.compareAndSet(true, false)) {
                                    routed.countDown();
                                    continue;
                                }
                                if (header.phase() == PerfUtil.PHASE_ACTIVE
                                    && System.nanoTime() < activeEnd) {
                                    metrics.recordNanos(header.latencyNanos());
                                }
                            }
                        }
                        if (stop) {
                            finished.countDown();
                            return;
                        }
                    }
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    finished.countDown();
                }
            }, "single-router-router-receiver");
            receiverThread.start();

            long probeDeadline = System.nanoTime() + Duration.ofSeconds(10).toNanos();
            while (System.nanoTime() < probeDeadline && routed.getCount() != 0) {
                try (Message probe = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_WARMUP, System.nanoTime())) {
                    if (!trySendActive(sender, targetRoute, probe)) {
                        Thread.onSpinWait();
                    }
                }
                try {
                    if (routed.await(10, java.util.concurrent.TimeUnit.MILLISECONDS)) {
                        break;
                    }
                } catch (InterruptedException ex) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException(
                        "router/router self-check interrupted", ex);
                }
            }
            PerfUtil.await(routed, "router/router self-check", Duration.ofSeconds(10));

            Thread traffic = new Thread(() -> {
                try {
                    try (Message active = PerfUtil.payloadTemplate(config.size())) {
                        while (System.nanoTime() < activeEnd) {
                            PerfUtil.resetAndWritePayload(active, config.size(),
                                (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime());
                            while (System.nanoTime() < activeEnd
                                && !trySendBlocking(sender, targetRoute, active)) {
                                Thread.onSpinWait();
                            }
                        }
                    }
                    // PERF_SINGLE_TEST_POLICY § 1.4: signal phase end with a
                    // wire-level stop token routed via the handshaken target
                    // route. ROUTER->ROUTER requires an explicit routing id.
                    sendRouterStopToken(sender, targetRoute);
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    finished.countDown();
                }
            }, "single-router-router-sender");
            traffic.start();
            PerfUtil.await(finished, "router/router receiver",
                Duration.ofSeconds(config.durationSeconds() + 30L));
            PerfUtil.join(traffic, "router/router sender", Duration.ofSeconds(10));
            PerfUtil.join(receiverThread, "router/router receiver thread",
                Duration.ofSeconds(10));
            if (failure.get() != null) {
                throw new IllegalStateException("router/router receiver failed",
                    failure.get());
            }
            PerfUtil.printSingleMonitorAutoHwm(config, receiverMonitor, "receiver",
                SocketType.ROUTER);
            PerfUtil.printSingleMonitorAutoHwm(config, senderMonitor, "sender",
                SocketType.ROUTER);
            return metrics.finishSingle(config);
        } finally {
            if (!sharedContext) {
                senderCtx.close();
            }
            receiverCtx.close();
        }
    }

    // C parity: perf_router_router.cpp performs a PING/PONG handshake before
    // the active phase and uses the observed peer routing id for active and
    // stop-token sends. Without this step, websocket ROUTER->ROUTER can report
    // connection readiness before the explicit route is usable.
    private static RoutingId performRouterRouterHandshake(RouterSocket receiver,
                                                          RouterSocket sender,
                                                          Duration timeout) {
        long deadline = System.nanoTime() + timeout.toNanos();
        RoutingId senderRoute = null;
        systems.zlink.contracts.Received received =
            new systems.zlink.contracts.Received();
        try {
            while (System.nanoTime() < deadline && senderRoute == null) {
                try (Message ping = Message.from(PING)) {
                    if (!trySendActive(sender, ROUTER1, ping)) {
                        Thread.onSpinWait();
                    }
                }
                while (recvIntoNoWait(receiver, received)) {
                    try {
                        if (Arrays.equals(received.firstPart().data(), PING)) {
                            senderRoute = received.routingId().orElse(ROUTER2);
                            break;
                        }
                    } finally {
                        received.close();
                    }
                }
                if (senderRoute == null) {
                    Thread.onSpinWait();
                }
            }
            if (senderRoute == null) {
                throw new IllegalStateException("router/router handshake ping timed out");
            }

            try (Message pong = Message.from(PONG)) {
                senderRouterSend(receiver, senderRoute, pong, SendFlags.NONE);
            }

            while (true) {
                try {
                    if (!sender.recv(received, RecvFlags.NONE)) {
                        continue;
                    }
                    try {
                        boolean ok = Arrays.equals(received.firstPart().data(), PONG);
                        if (!ok) {
                            throw new IllegalStateException(
                                "router/router handshake received unexpected payload");
                        }
                        return received.routingId().orElse(ROUTER1);
                    } finally {
                        received.close();
                    }
                } catch (RecvException ex) {
                    if (ex.getResult() == RecvResult.NO_DATA
                        || ex.getResult() == RecvResult.BUSY) {
                        continue;
                    }
                    throw ex;
                }
            }
        } finally {
            received.close();
        }
    }

    private static void drainRouterSocket(RouterSocket socket) {
        systems.zlink.contracts.Received received =
            new systems.zlink.contracts.Received();
        try {
            while (recvIntoNoWait(socket, received)) {
                received.close();
            }
        } finally {
            received.close();
        }
    }

    private static boolean recvIntoNoWait(RouterSocket socket,
                                          systems.zlink.contracts.Received received) {
        try {
            return socket.recv(received, RecvFlags.DONT_WAIT);
        } catch (RecvException ex) {
            if (ex.getResult() == RecvResult.NO_DATA
                || ex.getResult() == RecvResult.BUSY) {
                return false;
            }
            throw ex;
        }
    }

    // C parity: perf_router_router.cpp send_router_stop_token sends the
    // terminator with ZLINK_SEND_FLAGS_NONE and retries transient failures.
    // DONT_WAIT can lose the only stop token while the route is settling.
    private static void sendRouterStopToken(RouterSocket sender,
                                            RoutingId targetRoute) {
        PerfStopToken.sendWithRetry(() -> {
            try (Message stop = PerfStopToken.newMessage()) {
                senderRouterSend(sender, targetRoute, stop, SendFlags.NONE);
                return true;
            }
        }, "router/router");
    }

    private static void senderRouterSend(RouterSocket socket,
                                         RoutingId route,
                                         Message message,
                                         SendFlags flags) {
        socket.send(route)
            .message(message)
            .flags(flags)
            .submit();
    }

    private static boolean trySendActive(RouterSocket sender, RoutingId route,
                                         Message active) {
        try (Message outbound = Message.from(active)) {
            return sender.send(route)
                .message(outbound)
                .flags(SendFlags.DONT_WAIT)
                .submit();
        } catch (systems.zlink.contracts.SubmitException ex) {
            if (ex.getResult()
                == systems.zlink.contracts.SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        } catch (systems.zlink.contracts.ZlinkException ex) {
            int errno = ex.getInternalErrno();
            if (errno == 11 || errno == 4 || errno == 10035) {
                return false;
            }
            throw ex;
        }
    }

    private static boolean trySendBlocking(RouterSocket sender, RoutingId route,
                                           Message active) {
        try (Message outbound = Message.from(active)) {
            return sender.send(route)
                .message(outbound)
                .flags(SendFlags.NONE)
                .submit();
        } catch (systems.zlink.contracts.SubmitException ex) {
            if (ex.getResult()
                == systems.zlink.contracts.SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        } catch (systems.zlink.contracts.ZlinkException ex) {
            int errno = ex.getInternalErrno();
            if (errno == 11 || errno == 4 || errno == 10035
                || errno == 110 || errno == 113 || errno == 107) {
                return false;
            }
            throw ex;
        }
    }
}
