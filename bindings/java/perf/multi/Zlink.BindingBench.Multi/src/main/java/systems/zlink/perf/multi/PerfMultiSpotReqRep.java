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
import systems.zlink.contracts.RequestResult;
import systems.zlink.contracts.RoutingId;
import systems.zlink.contracts.SendFlags;
import systems.zlink.contracts.SpotDispatchEvent;
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

final class PerfMultiSpotReqRep {
    private static final RoutingId SERVER_NODE_RID =
        routingId("SPOT-REQREP-SERVER-NODE");
    private static final RoutingId SERVER_SPOT_RID =
        routingId("SPOT-REQREP-SERVER-SPOT");

    private PerfMultiSpotReqRep() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        AtomicBoolean stopRequested = new AtomicBoolean(false);
        AtomicInteger stopSeen = new AtomicInteger();
        AtomicReference<Throwable> failure = new AtomicReference<>();
        String controlEndpoint = derivedEndpoint(config.endpoint(), 1);
        try (Context ctx = PerfUtil.newContext(config);
             SpotNode node = new SpotNode(ctx);
             Spot replier = node.createSpot();
             PerfSpotDirectControl control = PerfSpotDirectControl.bind(
                 ctx, config, controlEndpoint, "reqrep-server")) {
            node.setRoutingId(SERVER_NODE_RID);
            replier.setRoutingId(SERVER_SPOT_RID);
            PerfUtil.applySpotOptions(node, config);
            PerfUtil.configureServerTls(node, config.transport());
            PerfUtil.configureClientTls(node, config.transport());
            node.bind(config.endpoint());
            PerfControl.emitReady(config.endpoint());
            PerfControl.emitControlReady(controlEndpoint);
            replier.onDispatchEvent(info -> {
                if (info.event() != SpotDispatchEvent.ROUTED_READABLE) {
                    return;
                }
                try {
                    drainServer(replier, config.clients(), stopSeen,
                        stopRequested);
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    stopRequested.set(true);
                }
            });
            awaitDirectControlStart(control, node, config,
                "spot reqrep server");
            PerfUtil.recalculateAutoHwm(ctx);
            PerfUtil.printMultiSpotNodeAutoHwm(config, node, "server");
            while (!stopRequested.get()) {
                Throwable ex = failure.get();
                if (ex != null) {
                    throw new IllegalStateException(
                        "spot reqrep dispatch drain failed", ex);
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
            PerfUtil.endpoint(config.transport(), "multi-spot-reqrep-control-client"),
            config.transport());
        String clientDataEndpoint = normalizeClientEndpoint(
            PerfUtil.endpoint(config.transport(), "multi-spot-reqrep-client"),
            config.transport());
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);

        try (Context ctx = PerfUtil.newContext(config);
             SpotNode node = new SpotNode(ctx);
             PerfSpotDirectControl control = PerfSpotDirectControl.bind(
                 ctx, config, clientControlEndpoint, "reqrep-client")) {
            node.setRoutingId(routingId("SPOT-REQREP-CLIENT-NODE"));
            PerfUtil.applySpotOptions(node, config);
            PerfUtil.configureServerTls(node, config.transport());
            PerfUtil.configureClientTls(node, config.transport());
            node.bind(clientDataEndpoint);
            node.connectPeer(endpoint);

            List<Spot> requesters = new ArrayList<>(config.clients());
            try {
                for (int i = 0; i < config.clients(); i++) {
                    Spot requester = node.createSpot();
                    requester.setRoutingId(routingId(
                        "SPOT-REQREP-CLIENT-SPOT-" + i));
                    requesters.add(requester);
                }
                control.connectPeer(serverControlEndpoint);
                PerfControl.emitClientControlEndpoint(clientControlEndpoint);
                PerfControl.awaitControlConnected(clientControlEndpoint,
                    "spot reqrep client");
                settleAfterReady();
                PerfUtil.recalculateAutoHwm(ctx);
                PerfUtil.printMultiSpotNodeAutoHwm(config, node, "client");
                control.publishDataEndpoint(clientDataEndpoint);
                waitForConnectedPeers(node, 1, config.connectReadyTimeoutMs(),
                    "spot reqrep client data link");
                control.publishConnected();
                control.publishReadyCount(config.size(), requesters.size());
                PerfControl.emitClientReady(config.size());
                PerfControl.awaitStart(config.size(), "spot reqrep client");
                control.waitStart(config.size(), config.connectReadyTimeoutMs());
                runClientWorkers(requesters, config, metrics);
                return metrics.finishMulti(config);
            } finally {
                for (Spot requester : requesters) {
                    requester.close();
                }
            }
        }
    }

    private static boolean drainServer(Spot replier,
                                    int expectedStops,
                                    AtomicInteger stopSeen,
                                    AtomicBoolean stopRequested) {
        boolean progressed = false;
        for (;;) {
            Received received = recvRoutedNoWait(replier);
            if (received == null) {
                return progressed;
            }
            progressed = true;
            try (received) {
                boolean stop = PerfStopToken.isStopTokenMessage(
                    received.firstPart());
                try (Message reply = received.firstPart().move()) {
                    received.reply().message(reply).submit();
                }
                if (stop && stopSeen.incrementAndGet() >= expectedStops) {
                    stopRequested.set(true);
                    return progressed;
                }
            }
        }
    }

    private static boolean handleServerReceived(Received received,
                                                int expectedStops,
                                                AtomicInteger stopSeen,
                                                AtomicBoolean stopRequested) {
        if (PerfStopToken.isStopTokenMessage(received.firstPart())) {
            if (stopSeen.incrementAndGet() >= expectedStops) {
                stopRequested.set(true);
            }
            return true;
        }
        return false;
    }

    private static void runClientWorkers(List<Spot> requesters,
                                         PerfUtil.Config config,
                                         PerfUtil.Metrics metrics) {
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfMultiSendLoops.runClients(requesters.size(), (index, durationSeconds) ->
            new Thread(() -> {
                try {
                    Spot requester = requesters.get(index);
                    long activeEnd = System.nanoTime()
                        + durationSeconds * 1_000_000_000L;
                    try (Message active = PerfUtil.payloadTemplate(config.size());
                         Poller poller = new Poller()) {
                        poller.add(requester, PollEventFlag.POLLIN,
                            PollEventFlag.POLLOUT);
                        while (System.nanoTime() < activeEnd) {
                            PerfUtil.resetAndWritePayload(active, config.size(),
                                (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime());
                            PerfUtil.Header reply = requestReply(requester,
                                poller, active, config.size(), activeEnd);
                            if (reply != null
                                && reply.phase() == PerfUtil.PHASE_ACTIVE
                                && System.nanoTime() < activeEnd) {
                                metrics.recordNanos(reply.latencyNanos() / 2L);
                            }
                        }
                    }
                    try (Message stop = PerfStopToken.newMessage()) {
                        sendToServer(requester, stop, SendFlags.NONE);
                    }
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    throw new IllegalStateException(ex);
                }
            }, "multi-spot-reqrep-client-" + index), config.durationSeconds());
        if (failure.get() != null) {
            throw new IllegalStateException("spot reqrep client failed",
                failure.get());
        }
    }

    private static void sendToServer(Spot requester,
                                     Message payload,
                                     SendFlags flags) {
        requester.sendToSpot(SERVER_NODE_RID, SERVER_SPOT_RID)
            .message(payload)
            .flags(flags)
            .submit();
    }

    private static PerfUtil.Header requestReply(Spot requester,
                                                Poller poller,
                                                Message payload,
                                                int expectedSize,
                                                long deadlineNs) {
        for (;;) {
            long remainingNs = deadlineNs - System.nanoTime();
            if (remainingNs <= 0L) {
                return null;
            }
            CountDownLatch done = new CountDownLatch(1);
            AtomicReference<PerfUtil.Header> replyRef = new AtomicReference<>();
            AtomicReference<RequestResult> resultRef = new AtomicReference<>();
            AtomicReference<Throwable> callbackFailure = new AtomicReference<>();
            boolean submitted = requester.requestToSpot(
                    SERVER_NODE_RID, SERVER_SPOT_RID)
                .message(payload)
                .timeout(Duration.ofNanos(remainingNs))
                .flags(SendFlags.DONT_WAIT)
                .submit((result, parts) -> {
                    try {
                        resultRef.set(result);
                        if (result == RequestResult.OK
                            && parts != null
                            && !parts.isEmpty()) {
                            replyRef.set(PerfUtil.decodeHeader(
                                parts.get(0), expectedSize));
                        }
                    } catch (Throwable ex) {
                        callbackFailure.compareAndSet(null, ex);
                    } finally {
                        Message.closeAll(parts);
                        done.countDown();
                    }
                });
            if (!submitted) {
                if (!waitFor(poller, PollEventFlag.POLLOUT, deadlineNs)) {
                    return null;
                }
                continue;
            }
            try {
                long waitMillis = Math.max(1L,
                    TimeUnit.NANOSECONDS.toMillis(remainingNs));
                if (!done.await(waitMillis, TimeUnit.MILLISECONDS)) {
                    return null;
                }
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("spot reqrep interrupted", ex);
            }
            Throwable failure = callbackFailure.get();
            if (failure != null) {
                throw new IllegalStateException("spot reqrep callback failed",
                    failure);
            }
            RequestResult result = resultRef.get();
            if (result == RequestResult.BUSY
                || result == RequestResult.NOT_CONNECTED
                || result == RequestResult.TIMED_OUT) {
                continue;
            }
            if (result != RequestResult.OK) {
                throw new IllegalStateException("spot reqrep failed: "
                    + result);
            }
            return replyRef.get();
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
            poller.wait(events, Duration.ofNanos(remainingNs));
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
            throw new IllegalStateException("spot reqrep sleep interrupted", ex);
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
