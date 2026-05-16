/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.Message;
import systems.zlink.contracts.PollEvent;
import systems.zlink.contracts.PollEventFlag;
import systems.zlink.contracts.Poller;
import systems.zlink.contracts.RecvException;
import systems.zlink.contracts.RecvFlags;
import systems.zlink.contracts.RecvResult;
import systems.zlink.contracts.Received;
import systems.zlink.contracts.RoutingId;
import systems.zlink.contracts.SendFlags;
import systems.zlink.contracts.SpotDispatchEvent;
import systems.zlink.contracts.SubmitException;
import systems.zlink.contracts.SubmitResult;
import systems.zlink.perf.PerfControl;
import systems.zlink.perf.PerfStopToken;
import systems.zlink.perf.PerfUtil;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.locks.LockSupport;

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
        AtomicReference<Throwable> failure = new AtomicReference<>();
        String controlEndpoint = derivedEndpoint(config.endpoint(), 1);
        try (Context ctx = PerfUtil.newContext(config);
             SpotNode node = new SpotNode(ctx);
             Spot spot = node.createSpot();
             PerfSpotDirectControl control = PerfSpotDirectControl.bind(
                 ctx, config, controlEndpoint, "sendsend-server")) {
            node.setRoutingId(SERVER_NODE_RID);
            spot.setRoutingId(SERVER_SPOT_RID);
            PerfUtil.applySpotOptions(node, config);
            PerfUtil.configureServerTls(node, config.transport());
            PerfUtil.configureClientTls(node, config.transport());
            node.bind(config.endpoint());
            PerfControl.emitReady(config.endpoint());
            PerfControl.emitControlReady(controlEndpoint);
            spot.onDispatchEvent(info -> {
                if (info.event() != SpotDispatchEvent.ROUTED_READABLE) {
                    return;
                }
                try {
                    drainServer(spot, config.clients(), stopSeen, stopped);
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    stopped.countDown();
                }
            });
            awaitDirectControlStart(control, node, config,
                "spot sendsend server");
            PerfUtil.recalculateAutoHwm(ctx);
            PerfUtil.printMultiSpotNodeAutoHwm(config, node, "server");
            while (stopped.getCount() != 0) {
                Throwable ex = failure.get();
                if (ex != null) {
                    throw new IllegalStateException(
                        "spot sendsend dispatch drain failed", ex);
                }
                sleepQuietly(1);
            }
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
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        try (Context ctx = PerfUtil.newContext(config);
             SpotNode node = new SpotNode(ctx);
             PerfSpotDirectControl control = PerfSpotDirectControl.bind(
                 ctx, config, clientControlEndpoint, "sendsend-client")) {
            node.setRoutingId(routingId("SPOT-SENDSEND-CLIENT-NODE"));
            PerfUtil.applySpotOptions(node, config);
            PerfUtil.configureServerTls(node, config.transport());
            PerfUtil.configureClientTls(node, config.transport());
            node.bind(clientDataEndpoint);
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
                                    CountDownLatch stopped) {
        boolean progressed = false;
        for (;;) {
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
                    received.send()
                        .message(reply)
                        .flags(SendFlags.DONT_WAIT)
                        .submit();
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
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfMultiSendLoops.runClients(spots.size(), (index, durationSeconds) ->
            new Thread(() -> {
                try {
                    Spot spot = spots.get(index);
                    long activeEnd = System.nanoTime()
                        + durationSeconds * 1_000_000_000L;
                    AtomicBoolean waitingReply = new AtomicBoolean();
                    spot.onDispatchEvent(info -> {
                        if (info.event() == SpotDispatchEvent.ROUTED_READABLE) {
                            drainClientReplies(spot, config.size(), metrics,
                                waitingReply, activeEnd);
                        }
                    });
                    try (Message active = PerfUtil.payloadTemplate(config.size());
                         Poller poller = new Poller()) {
                        poller.add(spot, PollEventFlag.POLLOUT);
                        while (System.nanoTime() < activeEnd) {
                            while (waitingReply.get()
                                   && System.nanoTime() < activeEnd) {
                                LockSupport.parkNanos(100_000L);
                            }
                            if (System.nanoTime() >= activeEnd) {
                                break;
                            }
                            PerfUtil.resetAndWritePayload(active, config.size(),
                                (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime());
                            waitingReply.set(true);
                            if (!sendToServerWhenWritable(spot, poller, active,
                                    activeEnd)) {
                                waitingReply.set(false);
                                break;
                            }
                        }
                    }
                    sendStopToServer(spot);
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    throw new IllegalStateException(ex);
                }
            }, "multi-spot-sendsend-client-" + index), config.durationSeconds());
        if (failure.get() != null) {
            throw new IllegalStateException("spot sendsend client failed",
                failure.get());
        }
    }

    private static void sendToServer(Spot spot, Message message,
                                     SendFlags flags) {
        spot.sendToSpot(SERVER_NODE_RID, SERVER_SPOT_RID)
            .message(message)
            .flags(flags)
            .submit();
    }

    private static boolean sendToServerWhenWritable(Spot spot,
                                                    Poller poller,
                                                    Message message,
                                                    long deadlineNs) {
        for (;;) {
            if (System.nanoTime() >= deadlineNs) {
                return false;
            }
            try {
                try (Message outbound = Message.copyOf(message)) {
                    sendToServer(spot, outbound, SendFlags.DONT_WAIT);
                }
                return true;
            } catch (SubmitException ex) {
                if (!isTransientSubmit(ex)) {
                    throw ex;
                }
                if (!waitFor(poller, PollEventFlag.POLLOUT, deadlineNs)) {
                    return false;
                }
            }
        }
    }

    private static void sendStopToServer(Spot spot) {
        try (Message stop = PerfStopToken.newMessage()) {
            sendToServer(spot, stop, SendFlags.NONE);
        }
    }

    private static boolean isTransientSubmit(SubmitException ex) {
        SubmitResult result = ex.getResult();
        return result == SubmitResult.BACKPRESSURED
            || result == SubmitResult.NOT_CONNECTED;
    }

    private static void drainClientReplies(Spot spot,
                                           int size,
                                           PerfUtil.Metrics metrics,
                                           AtomicBoolean waitingReply,
                                           long activeEnd) {
        for (;;) {
            Received received = recvRoutedNoWait(spot);
            if (received == null) {
                return;
            }
            try (received) {
                if (PerfStopToken.isStopTokenMessage(received.firstPart())) {
                    continue;
                }
                PerfUtil.Header reply = PerfUtil.decodeHeader(
                    received.firstPart(), size);
                waitingReply.set(false);
                if (reply.phase() == PerfUtil.PHASE_ACTIVE
                    && System.nanoTime() < activeEnd) {
                    metrics.recordNanos(reply.latencyNanos() / 2L);
                }
            }
        }
    }

    private static boolean waitFor(Poller poller, PollEventFlag expected,
                                   long deadlineNs) {
        List<PollEvent> events = new ArrayList<>(1);
        for (;;) {
            long remainingNs = deadlineNs - System.nanoTime();
            if (remainingNs <= 0) {
                return false;
            }
            poller.wait(events, Duration.ofMillis(-1));
            for (PollEvent event : events) {
                if (event.revents().contains(expected)) {
                    return true;
                }
            }
        }
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
                                                String label) {
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
                config.size(), config.clients(), config.connectReadyTimeoutMs(),
                endpoint -> dataNode.connectPeer(normalizeClientEndpoint(endpoint,
                    config.transport())));
            if (ready.dataEndpoints().isEmpty()) {
                throw new IllegalStateException(label + " missing data endpoint");
            }
            waitForConnectedPeers(dataNode, ready.dataEndpoints().size(),
                config.connectReadyTimeoutMs(), label + " data link");
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

    private static void waitForConnectedPeers(SpotNode node,
                                              int expectedPeers,
                                              int timeoutMs,
                                              String label) {
        long deadline = System.nanoTime()
            + Duration.ofMillis(Math.max(1, timeoutMs)).toNanos();
        while (System.nanoTime() < deadline) {
            if (node.statusSnapshot().connectedPeerCount() >= expectedPeers) {
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
        return RoutingId.fromBytes(value.getBytes(StandardCharsets.UTF_8));
    }

}
