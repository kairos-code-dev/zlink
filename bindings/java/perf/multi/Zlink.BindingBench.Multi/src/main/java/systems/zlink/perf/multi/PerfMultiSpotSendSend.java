/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.eventing.PollEventFlag;
import systems.zlink.contracts.eventing.PollEvents;
import systems.zlink.contracts.eventing.Poller;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.errors.RecvException;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RecvResult;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.sockets.SpotDispatchEvent;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.errors.SubmitException;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.perf.PerfControl;
import systems.zlink.perf.PerfStopToken;
import systems.zlink.perf.PerfUtil;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;

final class PerfMultiSpotSendSend {
    private static final RoutingId SERVER_NODE_RID =
        routingId("SPOT-SENDSEND-SERVER-NODE");
    private static final RoutingId SERVER_SPOT_RID =
        routingId("SPOT-SENDSEND-SERVER-SPOT");

    private PerfMultiSpotSendSend() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        CountDownLatch stopped = new CountDownLatch(1);
        AtomicInteger stopSeen = new AtomicInteger();
        AtomicLong activeEndRef = new AtomicLong(Long.MAX_VALUE);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        String controlEndpoint = derivedEndpoint(config.endpoint(), 1);
        String routerEndpoint = derivedEndpoint(config.endpoint(), 2);
        try (Context ctx = PerfUtil.newContext(config);
             SpotNode node = ctx.createSpotNode();
             Spot spot = node.createSpot();
             PerfSpotDirectControl control = PerfSpotDirectControl.bind(
                 ctx, config, controlEndpoint, "sendsend-server")) {
            node.setRoutingId(SERVER_NODE_RID);
            spot.setRoutingId(SERVER_SPOT_RID);
            PerfUtil.configureServerTls(node, config.transport());
            PerfUtil.configureClientTls(node, config.transport());
            node.setRouterBind(routerEndpoint);
            node.setPubBind(config.endpoint());
            PerfControl.emitReady(config.endpoint());
            PerfControl.emitControlReady(controlEndpoint);
            int expectedStops = activeSpotSlotLimit(config.clients(),
                config.size());
            spot.onDispatchEvent(info -> {
                if (info.event() != SpotDispatchEvent.ROUTED_READABLE) {
                    return;
                }
                long activeEnd = activeEndRef.get();
                if (System.nanoTime() >= activeEnd) {
                    stopped.countDown();
                    return;
                }
                try {
                    drainServer(spot, expectedStops, stopSeen, stopped,
                        activeEnd);
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    stopped.countDown();
                }
            });
            awaitDirectControlStart(control, node, config,
                "spot sendsend server", spotServerReadyTimeoutMs(config));
            PerfUtil.recalculateAutoHwm(ctx);
            PerfUtil.printMultiSpotNodeAutoHwm(config, node, "server");
            long activeEnd = System.nanoTime()
                + config.durationSeconds() * 1_000_000_000L;
            activeEndRef.set(activeEnd);
            while (stopped.getCount() != 0 && System.nanoTime() < activeEnd) {
                Throwable ex = failure.get();
                if (ex != null) {
                    throw new IllegalStateException(
                        "spot sendsend dispatch drain failed", ex);
                }
                sleepQuietly(1);
            }
            stopped.countDown();
            return PerfUtil.Result.silent(config);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        String endpoint = normalizeClientEndpoint(config.endpoint(),
            config.transport());
        String serverControlEndpoint = normalizeClientEndpoint(
            derivedEndpoint(config.endpoint(), 1), config.transport());
        String clientControlEndpoint = normalizeClientEndpoint(
            PerfUtil.endpoint(config.transport(), "multi-spot-sendsend-control-client"),
            config.transport());
        String clientDataEndpoint = normalizeClientEndpoint(
            PerfUtil.endpoint(config.transport(), "multi-spot-sendsend-client"),
            config.transport());
        String clientRouterEndpoint = normalizeClientEndpoint(
            PerfUtil.endpoint(config.transport(), "multi-spot-sendsend-client-router"),
            config.transport());
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        try (Context ctx = PerfUtil.newContext(config);
             SpotNode node = ctx.createSpotNode();
             PerfSpotDirectControl control = PerfSpotDirectControl.bind(
                 ctx, config, clientControlEndpoint, "sendsend-client")) {
            node.setRoutingId(routingId("SPOT-SENDSEND-CLIENT-NODE"));
            PerfUtil.configureServerTls(node, config.transport());
            PerfUtil.configureClientTls(node, config.transport());
            node.setRouterBind(clientRouterEndpoint);
            node.setPubBind(clientDataEndpoint);
            node.connectPeer(endpoint);

            List<Spot> spots = new ArrayList<>(config.clients());
            try {
                control.connectPeer(serverControlEndpoint);
                PerfControl.emitClientControlEndpoint(clientControlEndpoint);
                PerfControl.awaitControlConnected(clientControlEndpoint,
                    "spot sendsend client");
                for (int i = 0; i < config.clients(); i++) {
                    Spot spot = node.createSpot();
                    spot.setRoutingId(routingId("SPOT-SENDSEND-CLIENT-SPOT-" + i));
                    spots.add(spot);
                }
                settleAfterReady();
                PerfUtil.recalculateAutoHwm(ctx);
                PerfUtil.printMultiSpotNodeAutoHwm(config, node, "client");
                control.publishDataEndpoint(clientDataEndpoint);
                waitForConnectedPeers(node, 1, config.connectReadyTimeoutMs(),
                    "spot sendsend client data link");
                control.publishConnected();
                control.publishReadyCount(config.size(), config.clients());
                PerfControl.emitClientReady(config.size());
                PerfControl.awaitStart(config.size(), "spot sendsend client");
                control.waitStart(config.size(), config.connectReadyTimeoutMs());
                runClientWorkers(spots, config, metrics);
                return metrics.finishMulti(config);
            } finally {
                for (Spot spot : spots) {
                    spot.close();
                }
            }
        }
    }

    private static boolean drainServer(Spot spot,
                                    int expectedStops,
                                    AtomicInteger stopSeen,
                                    CountDownLatch stopped,
                                    long activeEnd) {
        boolean progressed = false;
        for (;;) {
            if (System.nanoTime() >= activeEnd) {
                stopped.countDown();
                return progressed;
            }
            Received received = recvRoutedNoWait(spot);
            if (received == null) {
                return progressed;
            }
            progressed = true;
            try (received) {
                if (PerfStopToken.isStopTokenMessage(received.firstPart())) {
                    if (stopSeen.incrementAndGet() >= expectedStops) {
                        stopped.countDown();
                    }
                    continue;
                }
                try (Message reply = received.firstPart().move()) {
                    try {
                        received.send()
                            .message(reply)
                            .flags(SendFlags.DONT_WAIT)
                            .submit();
                    } catch (SubmitException ex) {
                        if (!isTransientSubmit(ex)) {
                            throw ex;
                        }
                    }
                }
            }
        }
    }

    private static void awaitStopped(CountDownLatch stopped,
                                     AtomicReference<Throwable> failure,
                                     String label) {
        try {
            while (!stopped.await(100, TimeUnit.MILLISECONDS)) {
                Throwable ex = failure.get();
                if (ex != null) {
                    throw new IllegalStateException(label, ex);
                }
            }
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException(label, ex);
        }
        Throwable ex = failure.get();
        if (ex != null) {
            throw new IllegalStateException(label, ex);
        }
    }

    private static void runClientWorkers(List<Spot> spots,
                                         PerfUtil.Config config,
                                         PerfUtil.Metrics metrics) {
        int n = spots.size();
        int msgSize = config.size();
        int activeSlots = activeSpotSlotLimit(n, msgSize);
        Message[] payloads = new Message[n];
        boolean[] waitingReply = new boolean[n];
        PollEvents events = new PollEvents(Math.max(1, n));
        for (int i = 0; i < n; i++) {
            payloads[i] = PerfUtil.payloadTemplate(msgSize);
        }
        try (Poller poller = Zlink.createPoller()) {
            for (int i = 0; i < n; i++) {
                poller.add(spots.get(i), i, PollEventFlag.POLLIN);
            }
            long activeEnd = System.nanoTime()
                + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                boolean sendProgress = false;
                boolean hasWaitingReply = false;
                for (int i = 0; i < activeSlots; i++) {
                    if (waitingReply[i]) {
                        hasWaitingReply = true;
                        continue;
                    }
                    PerfUtil.resetAndWritePayload(payloads[i], msgSize,
                        (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime());
                    waitingReply[i] = true;
                    if (trySendToServer(spots.get(i), payloads[i],
                            SendFlags.DONT_WAIT)) {
                        sendProgress = true;
                    } else {
                        waitingReply[i] = false;
                    }
                }
                if (sendProgress) {
                    continue;
                }
                long remainingNs = activeEnd - System.nanoTime();
                if (remainingNs <= 0L) {
                    break;
                }
                Duration waitDuration = Duration.ofNanos(
                    Math.max(1L, remainingNs));
                int count = poller.wait(events, waitDuration);
                for (int i = 0; i < count; i++) {
                    if (!events.hasEvent(i, PollEventFlag.POLLIN)) {
                        continue;
                    }
                    int index = (int) events.slot(i);
                    if (index < 0 || index >= activeSlots)
                        continue;
                    drainClientReply(spots.get(index), msgSize, metrics,
                        waitingReply, index, activeEnd);
                }
            }
        } finally {
            Message.closeAll(List.of(payloads));
            for (Spot spot : spots) {
                sendStopToServer(spot);
            }
        }
    }

    private static void sendToServer(Spot spot, Message message,
                                     SendFlags flags) {
        spot.sendToSpot(SERVER_NODE_RID, SERVER_SPOT_RID)
            .message(message)
            .flags(flags)
            .submit();
    }

    private static boolean trySendToServer(Spot spot, Message message,
                                           SendFlags flags) {
        try {
            sendToServer(spot, message, flags);
            return true;
        } catch (SubmitException ex) {
            if (isTransientSubmit(ex)) {
                return false;
            }
            throw ex;
        }
    }

    private static void sendStopToServer(Spot spot) {
        try (Message stop = PerfStopToken.newMessage()) {
            sendToServer(spot, stop, SendFlags.DONT_WAIT);
        } catch (SubmitException ex) {
            if (!isTransientSubmit(ex)) {
                throw ex;
            }
        }
    }

    private static boolean isTransientSubmit(SubmitException ex) {
        SubmitResult result = ex.getResult();
        return result == SubmitResult.BACKPRESSURED
            || result == SubmitResult.NOT_CONNECTED;
    }

    private static void drainClientReply(Spot spot,
                                         int size,
                                         PerfUtil.Metrics metrics,
                                         boolean[] waitingReply,
                                         int index,
                                         long activeEnd) {
        Received received = recvRoutedNoWait(spot);
        if (received == null) {
            return;
        }
        try (received) {
            if (PerfStopToken.isStopTokenMessage(received.firstPart())) {
                return;
            }
            PerfUtil.Header reply = PerfUtil.decodeHeader(
                received.firstPart(), size);
            waitingReply[index] = false;
            if (reply.phase() == PerfUtil.PHASE_ACTIVE
                && System.nanoTime() < activeEnd) {
                metrics.recordNanos(reply.latencyNanos() / 2L);
            }
        }
    }

    private static int activeSpotSlotLimit(int totalSlots, int msgSize) {
        if (msgSize >= 131_072)
            return Math.min(totalSlots, 8);
        if (msgSize >= 65_536)
            return Math.min(totalSlots, 32);
        return totalSlots;
    }

    private static Received recvRoutedNoWait(Spot spot) {
        try {
            Received received = new Received();
            return spot.recvRouted(received, RecvFlags.DONT_WAIT) ? received : null;
        } catch (RecvException ex) {
            if (ex.getResult() == RecvResult.NO_DATA
                || ex.getResult() == RecvResult.BUSY) {
                return null;
            }
            throw ex;
        }
    }

    private static void awaitDirectControlStart(PerfSpotDirectControl control,
                                                SpotNode dataNode,
                                                PerfUtil.Config config,
                                                String label,
                                                int timeoutMs) {
        String expectedStart = "START," + config.size();
        try (BufferedReader reader = new BufferedReader(
                 new InputStreamReader(System.in, StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                if (line.startsWith("CONNECT_CONTROL,")) {
                    String endpoint = line.substring("CONNECT_CONTROL,".length());
                    control.connectPeer(endpoint);
                    PerfControl.emitControlConnected(endpoint);
                    break;
                }
            }
            PerfSpotDirectControl.ReadyState ready = control.waitReady(
                config.size(), config.clients(), timeoutMs,
                endpoint -> dataNode.connectPeer(normalizeClientEndpoint(endpoint,
                    config.transport())));
            if (ready.dataEndpoints().isEmpty()) {
                throw new IllegalStateException(label + " missing data endpoint");
            }
            waitForConnectedPeers(dataNode, ready.dataEndpoints().size(),
                timeoutMs, label + " data link");
            while ((line = reader.readLine()) != null) {
                if (expectedStart.equals(line)) {
                    control.publishStart(config.size());
                    return;
                }
            }
        } catch (java.io.IOException ex) {
            throw new IllegalStateException(label + " control read failed", ex);
        }
        throw new IllegalStateException(label + " missing " + expectedStart);
    }

    private static int spotServerReadyTimeoutMs(PerfUtil.Config config) {
        int connectTimeoutMs = Math.max(1, config.connectReadyTimeoutMs());
        return Math.max(connectTimeoutMs, Math.max(1000, connectTimeoutMs * 6));
    }

    private static void waitForConnectedPeers(SpotNode node,
                                              int expectedPeers,
                                              int timeoutMs,
                                              String label) {
        long deadline = System.nanoTime()
            + Duration.ofMillis(Math.max(1, timeoutMs)).toNanos();
        while (System.nanoTime() < deadline) {
            if (node.status().connectedPeerCount() >= expectedPeers) {
                return;
            }
            sleepQuietly(10);
        }
        throw new IllegalStateException(label + " connected peer timeout");
    }

    private static void settleAfterReady() {
        int settleMs = PerfUtil.intEnv("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000);
        if (settleMs > 0) {
            sleepQuietly(settleMs);
        }
    }

    private static void sleepQuietly(int millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("spot sendsend sleep interrupted",
                ex);
        }
    }

    private static String normalizeClientEndpoint(String endpoint,
                                                  String transport) {
        if (!"tls".equals(transport) && !"wss".equals(transport)) {
            return endpoint;
        }
        return endpoint.replace("://127.0.0.1:", "://localhost:");
    }

    private static String derivedEndpoint(String endpoint, int portOffset) {
        int schemeSep = endpoint.indexOf("://");
        int colon = endpoint.lastIndexOf(':');
        if (schemeSep <= 0 || colon <= schemeSep + 2
            || colon == endpoint.length() - 1) {
            throw new IllegalArgumentException(
                "cannot derive endpoint from: " + endpoint);
        }
        int port = Integer.parseInt(endpoint.substring(colon + 1));
        return endpoint.substring(0, colon + 1) + (port + portOffset);
    }

    private static RoutingId routingId(String value) {
        return RoutingId.from(value.getBytes(StandardCharsets.UTF_8));
    }

}
