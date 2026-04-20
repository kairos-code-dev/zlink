/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.SpotDispatchEvent;
import dev.kairoscode.zlink.SubmitException;
import dev.kairoscode.zlink.SubmitResult;
import dev.kairoscode.zlink.perf.PerfControl;
import dev.kairoscode.zlink.perf.PerfSocketPollSet;
import dev.kairoscode.zlink.perf.PerfUtil;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.registry.ServiceType;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

final class PerfMultiSpotReqRep {
    private static final String SERVICE_NAME = "perf.spot.reqrep.service";
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();
    private static final RoutingId SERVER_NODE_ID = RoutingId.fromBytes(
        "PERF_SPOT_REQREP_NODE".getBytes(StandardCharsets.UTF_8));
    private static final RoutingId SERVER_SPOT_ID = RoutingId.fromBytes(
        "PERF_SPOT_REQREP_SPOT".getBytes(StandardCharsets.UTF_8));

    private PerfMultiSpotReqRep() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        String controlEndpoint = PerfUtil.derivedControlEndpoint(config.endpoint());
        CountDownLatch finished = new CountDownLatch(config.clients());
        AtomicReference<Throwable> callbackFailure = new AtomicReference<>();
        try (Context ctx = PerfUtil.newContext(config);
             Registry registry = new Registry(ctx);
             Discovery discovery = new Discovery(ctx, ServiceType.SPOT, SERVICE_NAME);
             DealerSocket channelDealer = new DealerSocket(ctx);
             SpotNode node = new SpotNode(ctx);
             Spot replier = node.createSpot();
             RouterSocket control = new RouterSocket(ctx)) {
            String registryPub = PerfUtil.endpoint(config.transport(),
                "multi-spot-reqrep-registry-pub");
            String registryRouter = PerfUtil.endpoint(config.transport(),
                "multi-spot-reqrep-registry-router");
            node.setRoutingId(SERVER_NODE_ID);
            replier.setRoutingId(SERVER_SPOT_ID);
            registry.bind(registryPub, registryRouter);
            discovery.connectRegistry(registryRouter);
            node.attachChannelDealerManual(SERVICE_NAME, channelDealer);
            node.attachDiscovery(discovery);
            PerfUtil.applySpotOptions(node, config);
            PerfUtil.applySocketOptions(control, config);
            PerfUtil.configureServerTls(node, config.transport());
            PerfUtil.configureServerTls(control, config.transport());
            node.bind(config.endpoint());
            control.bind(controlEndpoint);
            replier.onDispatchEvent(event -> {
                if (event != SpotDispatchEvent.ROUTED_READABLE) {
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
                            if (header.phase() == PerfUtil.PHASE_COOLDOWN) {
                                finished.countDown();
                            }
                        }
                    }
                } catch (Throwable ex) {
                    callbackFailure.compareAndSet(null, ex);
                    while (finished.getCount() > 0L) {
                        finished.countDown();
                    }
                }
            });
            PerfControl.emitReady(config.endpoint());
            PerfControl.emitControlReady(controlEndpoint);
            PerfControl.awaitStart(config.size(), "spot reqrep server");
            List<RoutingId> readyPeers = waitForReadyCounts(control, config);
            broadcastControlStart(control, readyPeers, config.size());
            PerfUtil.await(finished, "spot reqrep server",
                Duration.ofSeconds(config.durationSeconds() + 20L));
            if (callbackFailure.get() != null) {
                throw new IllegalStateException("spot reqrep replier failed",
                    callbackFailure.get());
            }
            return PerfUtil.Result.silent(config);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        String controlEndpoint = normalizeClientEndpoint(
            PerfUtil.derivedControlEndpoint(config.endpoint()), config.transport());
        String dataEndpoint = normalizeClientEndpoint(config.endpoint(), config.transport());
        CountDownLatch ready = new CountDownLatch(config.clients());
        CountDownLatch runnerStarted = new CountDownLatch(1);
        CountDownLatch controlStarted = new CountDownLatch(config.clients());
        CountDownLatch go = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        PerfMultiSendLoops.runClients(config.clients(), (index, duration) ->
            new Thread(() -> runClientSlot(config, dataEndpoint, controlEndpoint, duration,
                ready, runnerStarted, controlStarted, go, metrics, failure),
                "multi-spot-reqrep-client-" + index),
            config.durationSeconds());
        if (failure.get() != null) {
            throw new IllegalStateException("spot reqrep client failed", failure.get());
        }
        return metrics.finishMulti(config);
    }

    private static void runClientSlot(PerfUtil.Config config,
                                      String dataEndpoint,
                                      String controlEndpoint,
                                      int duration,
                                      CountDownLatch ready,
                                      CountDownLatch runnerStarted,
                                      CountDownLatch controlStarted,
                                      CountDownLatch go,
                                      PerfUtil.Metrics metrics,
                                      AtomicReference<Throwable> failure) {
        try (Context ctx = PerfUtil.newContext(config);
             DealerSocket control = new DealerSocket(ctx);
             var controlMonitor = control.monitorOpen(MonitorEventType.CONNECTION_READY);
             RouterSocket requester = new RouterSocket(ctx);
             PerfSocketPollSet requesterPollSet = PerfSocketPollSet.fromSockets(
                 List.of(requester), PollEventType.POLLIN.getValue())) {
            PerfUtil.applyMonitorOptions(controlMonitor, config);
            PerfUtil.applySocketOptions(control, config);
            PerfUtil.applySocketOptions(requester, config);
            PerfUtil.configureClientTls(control, config.transport());
            PerfUtil.configureClientTls(requester, config.transport());
            control.connect(controlEndpoint);
            PerfUtil.waitForMonitorEvent(controlMonitor, READY_EVENTS, 1,
                Duration.ofMillis(config.connectReadyTimeoutMs()),
                "spot reqrep control ready");
            requester.connect(dataEndpoint);
            PerfControl.emitControlConnected(controlEndpoint);
            settleReadyBarrier();
            sendControl(control, "CONNECTED");
            settleControlBarrier();
            sendControl(control, "READY_COUNT," + config.size() + ",1");
            ready.countDown();
            if (ready.getCount() == 0L) {
                PerfControl.emitClientReady(config.size());
                PerfControl.awaitStart(config.size(), "spot reqrep client");
                runnerStarted.countDown();
            }
            awaitControlStart(control, config.size(), config.connectReadyTimeoutMs());
            controlStarted.countDown();
            if (controlStarted.getCount() == 0L) {
                PerfUtil.await(runnerStarted, "spot reqrep runner start",
                    Duration.ofSeconds(10));
                metrics.startActiveWindow();
                go.countDown();
            }
            PerfUtil.await(go, "spot reqrep start", Duration.ofSeconds(10));

            try (Message active = PerfUtil.payloadTemplate(config.size());
                 Message cooldown = PerfUtil.payloadTemplate(config.size())) {
                long activeEnd = System.nanoTime() + duration * 1_000_000_000L;
                while (System.nanoTime() < activeEnd) {
                    PerfUtil.writePayload(active, config.size(),
                        (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime());
                    sendWhenWritable(requester, requesterPollSet, active,
                        System.nanoTime() + Duration.ofSeconds(2).toNanos());
                    PerfUtil.Header reply = awaitReply(requester, requesterPollSet, config,
                        activeEnd + Duration.ofSeconds(2).toNanos());
                    if (reply.phase() == PerfUtil.PHASE_ACTIVE) {
                        metrics.recordNanos(reply.latencyNanos() / 2L);
                    }
                }
                PerfUtil.writePayload(cooldown, config.size(),
                    (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime());
                sendWhenWritable(requester, requesterPollSet, cooldown,
                    System.nanoTime() + Duration.ofSeconds(5).toNanos());
                awaitReply(requester, requesterPollSet, config,
                    System.nanoTime() + Duration.ofSeconds(5).toNanos());
            }
        } catch (Throwable ex) {
            failure.compareAndSet(null, ex);
        }
    }

    private static void sendWhenWritable(RouterSocket requester,
                                         PerfSocketPollSet pollSet,
                                         Message payload,
                                         long deadlineNs) {
        while (true) {
            try {
                requester.sendToSpot(SERVER_NODE_ID, SERVER_SPOT_ID, payload,
                    SendFlags.DONT_WAIT);
                return;
            } catch (SubmitException ex) {
                if (ex.getResult() != SubmitResult.BACKPRESSURED) {
                    throw ex;
                }
            }
            long remainingNs = Math.max(1L, deadlineNs - System.nanoTime());
            if (remainingNs <= 0L) {
                throw new IllegalStateException("spot reqrep send timed out");
            }
            pollSet.setEvents(0, PollEventType.POLLOUT.getValue());
            pollSet.poll(Math.max(1, (int) Math.min(Integer.MAX_VALUE,
                Duration.ofNanos(remainingNs).toMillis())));
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
            pollSet.setEvents(0, PollEventType.POLLIN.getValue());
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

    private static List<RoutingId> waitForReadyCounts(RouterSocket control,
                                                      PerfUtil.Config config) {
        int readyUnits = 0;
        List<RoutingId> readyPeers = new ArrayList<>();
        long deadlineNs = System.nanoTime()
            + Duration.ofMillis(Math.max(10_000L,
                config.connectReadyTimeoutMs() * 6L)).toNanos();
        try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
            List.of(control), PollEventType.POLLIN.getValue())) {
            while (readyUnits < config.clients() && System.nanoTime() < deadlineNs) {
                long remainingNs = Math.max(1L, deadlineNs - System.nanoTime());
                int timeoutMs = Math.max(1, (int) Math.min(Integer.MAX_VALUE,
                    Duration.ofNanos(remainingNs).toMillis()));
                if (pollSet.poll(timeoutMs) <= 0
                    || !pollSet.isReady(0, PollEventType.POLLIN.getValue())) {
                    continue;
                }
                while (true) {
                    dev.kairoscode.zlink.Received received = PerfUtil.recvNoWait(control);
                    if (received == null) {
                        break;
                    }
                    try (received) {
                        RoutingId peer = received.routingIdOrThrow();
                        String line = new String(received.firstPart().data(),
                            StandardCharsets.UTF_8);
                        if ("CONNECTED".equals(line)) {
                            rememberPeer(readyPeers, peer);
                            continue;
                        }
                        int increment = parseReadyCount(line, config.size());
                        if (increment <= 0) {
                            continue;
                        }
                        rememberPeer(readyPeers, peer);
                        readyUnits += increment;
                    }
                }
            }
        }
        if (readyUnits < config.clients()) {
            throw new IllegalStateException("spot reqrep ready count timed out");
        }
        return readyPeers;
    }

    private static void broadcastControlStart(RouterSocket control,
                                              List<RoutingId> peers,
                                              int size) {
        byte[] payload = ("START," + size).getBytes(StandardCharsets.UTF_8);
        for (RoutingId peer : peers) {
            try (Message message = Message.copyOf(payload)) {
                control.send(peer, message);
            }
        }
    }

    private static void awaitControlStart(DealerSocket control,
                                          int size,
                                          int readyTimeoutMs) {
        long deadlineNs = System.nanoTime()
            + Duration.ofMillis(Math.max(10_000L, readyTimeoutMs * 6L)).toNanos();
        try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
            List.of(control), PollEventType.POLLIN.getValue())) {
            while (System.nanoTime() < deadlineNs) {
                long remainingNs = Math.max(1L, deadlineNs - System.nanoTime());
                int timeoutMs = Math.max(1, (int) Math.min(Integer.MAX_VALUE,
                    Duration.ofNanos(remainingNs).toMillis()));
                if (pollSet.poll(timeoutMs) <= 0
                    || !pollSet.isReady(0, PollEventType.POLLIN.getValue())) {
                    continue;
                }
                while (true) {
                    dev.kairoscode.zlink.Received received = PerfUtil.recvNoWait(control);
                    if (received == null) {
                        break;
                    }
                    try (received) {
                        String line = new String(received.firstPart().data(),
                            StandardCharsets.UTF_8);
                        if (("START," + size).equals(line)) {
                            return;
                        }
                    }
                }
            }
        }
        throw new IllegalStateException("spot reqrep control start timed out");
    }

    private static void sendControl(DealerSocket control, String line) {
        try (Message message = Message.copyOf(line.getBytes(StandardCharsets.UTF_8))) {
            control.send(message);
        }
    }

    private static int parseReadyCount(String line, int size) {
        String[] parts = line.split(",", -1);
        if (parts.length != 3 || !"READY_COUNT".equals(parts[0])) {
            return 0;
        }
        int parsedSize = Integer.parseInt(parts[1]);
        int count = Integer.parseInt(parts[2]);
        return parsedSize == size && count > 0 ? count : 0;
    }

    private static void rememberPeer(List<RoutingId> peers, RoutingId peer) {
        for (RoutingId current : peers) {
            if (current.equals(peer)) {
                return;
            }
        }
        peers.add(peer);
    }

    private static void settleReadyBarrier() {
        int settleMs = PerfUtil.intEnv("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000);
        if (settleMs <= 0) {
            return;
        }
        sleepQuietly(settleMs, "spot reqrep ready barrier interrupted");
    }

    private static void settleControlBarrier() {
        int settleMs = PerfUtil.intEnv("PERF_MULTI_SPOT_CONTROL_SETTLE_MS", 25);
        if (settleMs <= 0) {
            return;
        }
        sleepQuietly(settleMs, "spot reqrep control barrier interrupted");
    }

    private static void sleepQuietly(int millis, String label) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException(label, ex);
        }
    }

    private static String normalizeClientEndpoint(String endpoint, String transport) {
        if (!"tls".equals(transport)) {
            return endpoint;
        }
        return endpoint.replace("://127.0.0.1:", "://localhost:");
    }
}
