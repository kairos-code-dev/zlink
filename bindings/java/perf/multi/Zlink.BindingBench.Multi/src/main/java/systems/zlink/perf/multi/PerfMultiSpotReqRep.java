/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.Message;
import systems.zlink.contracts.PollEvents;
import systems.zlink.contracts.PollEventFlag;
import systems.zlink.contracts.Poller;
import systems.zlink.contracts.RecvException;
import systems.zlink.contracts.RecvFlags;
import systems.zlink.contracts.RecvResult;
import systems.zlink.contracts.Received;
import systems.zlink.contracts.RequestCallback;
import systems.zlink.contracts.RequestResult;
import systems.zlink.contracts.RoutingId;
import systems.zlink.contracts.SendFlags;
import systems.zlink.contracts.SpotDispatchEvent;
import systems.zlink.contracts.SubmitException;
import systems.zlink.contracts.SubmitResult;
import systems.zlink.contracts.Timer;
import systems.zlink.perf.PerfControl;
import systems.zlink.perf.PerfUtil;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;

final class PerfMultiSpotReqRep {
    private static final long ACTIVE_DEADLINE_SLOT = Integer.MAX_VALUE;
    private static final RoutingId SERVER_NODE_RID =
        routingId("SPOT-REQREP-SERVER-NODE");
    private static final RoutingId SERVER_SPOT_RID =
        routingId("SPOT-REQREP-SERVER-SPOT");

    private PerfMultiSpotReqRep() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        AtomicBoolean stopRequested = new AtomicBoolean(false);
        AtomicLong activeEndRef = new AtomicLong(Long.MAX_VALUE);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        String controlEndpoint = derivedEndpoint(config.endpoint(), 1);
        String routerEndpoint = derivedEndpoint(config.endpoint(), 2);
        try (Context ctx = PerfUtil.newContext(config);
             SpotNode node = new SpotNode(ctx);
             Spot replier = node.createSpot();
             PerfSpotDirectControl control = PerfSpotDirectControl.bind(
                 ctx, config, controlEndpoint, "reqrep-server")) {
            node.setRoutingId(SERVER_NODE_RID);
            replier.setRoutingId(SERVER_SPOT_RID);
            PerfUtil.configureServerTls(node, config.transport());
            PerfUtil.configureClientTls(node, config.transport());
            node.setRouterBind(routerEndpoint);
            node.setPubBind(config.endpoint());
            PerfControl.emitReady(config.endpoint());
            PerfControl.emitControlReady(controlEndpoint);
            replier.onDispatchEvent(info -> {
                if (info.event() != SpotDispatchEvent.ROUTED_READABLE) {
                    return;
                }
                long activeEnd = activeEndRef.get();
                if (stopRequested.get() || System.nanoTime() >= activeEnd) {
                    return;
                }
                try {
                    drainServer(replier, activeEnd);
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    stopRequested.set(true);
                }
            });
            awaitDirectControlStart(control, node, config,
                "spot reqrep server", spotServerReadyTimeoutMs(config));
            PerfUtil.recalculateAutoHwm(ctx);
            PerfUtil.printMultiSpotNodeAutoHwm(config, node, "server");
            long activeEnd = System.nanoTime()
                + Duration.ofSeconds(config.durationSeconds()).toNanos();
            activeEndRef.set(activeEnd);
            while (!stopRequested.get() && System.nanoTime() < activeEnd) {
                Throwable ex = failure.get();
                if (ex != null) {
                    throw new IllegalStateException(
                        "spot reqrep dispatch drain failed", ex);
                }
                sleepQuietly(1);
            }
            stopRequested.set(true);
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
        String clientRouterEndpoint = normalizeClientEndpoint(
            PerfUtil.endpoint(config.transport(), "multi-spot-reqrep-client-router"),
            config.transport());
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);

        try (Context ctx = PerfUtil.newContext(config);
             SpotNode node = new SpotNode(ctx);
             PerfSpotDirectControl control = PerfSpotDirectControl.bind(
                 ctx, config, clientControlEndpoint, "reqrep-client")) {
            node.setRoutingId(routingId("SPOT-REQREP-CLIENT-NODE"));
            PerfUtil.configureServerTls(node, config.transport());
            PerfUtil.configureClientTls(node, config.transport());
            node.setRouterBind(clientRouterEndpoint);
            node.setPubBind(clientDataEndpoint);
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

    private static boolean drainServer(Spot replier, long activeEnd) {
        boolean progressed = false;
        Received received = new Received();
        try {
            for (;;) {
                if (System.nanoTime() >= activeEnd) {
                    return progressed;
                }
                if (!recvRoutedNoWait(replier, received)) {
                    return progressed;
                }
                progressed = true;
                try {
                    try (Message reply = received.firstPart().move()) {
                        try {
                            received.reply().message(reply).submit();
                        } catch (SubmitException ex) {
                            if (!isTransientSubmit(ex)) {
                                throw ex;
                            }
                        }
                    }
                } finally {
                    received.close();
                }
            }
        } finally {
            received.close();
        }
    }

    private static void runClientWorkers(List<Spot> requesters,
                                         PerfUtil.Config config,
                                         PerfUtil.Metrics metrics) {
        int activeClients = activeSpotSlotLimit(requesters.size(), config.size());
        Message[] payloads = new Message[activeClients];
        AtomicBoolean[] waitingReply = new AtomicBoolean[activeClients];
        AtomicReference<Throwable> failure = new AtomicReference<>();
        for (int i = 0; i < activeClients; i++) {
            payloads[i] = PerfUtil.payloadTemplate(config.size());
            waitingReply[i] = new AtomicBoolean();
        }
        try (Poller completionPoller = new Poller();
             Timer activeTimer = new Timer()) {
            PollEvents completionEvents = new PollEvents(
                Math.max(1, activeClients + 1));
            for (int i = 0; i < activeClients; i++) {
                completionPoller.add(requesters.get(i), i,
                    PollEventFlag.POLLCOMPLETION);
            }
            completionPoller.add(activeTimer, ACTIVE_DEADLINE_SLOT);
            long activeEnd = System.nanoTime()
                + config.durationSeconds() * 1_000_000_000L;
            activeTimer.start(Duration.ofSeconds(
                Math.max(1, config.durationSeconds())), 1);
            Duration requestTimeout = Duration.ofMillis(
                Math.max(1, config.recvTimeoutMs()));
            RequestMetricsCallback[] callbacks =
                new RequestMetricsCallback[activeClients];
            for (int i = 0; i < activeClients; i++) {
                callbacks[i] = new RequestMetricsCallback(waitingReply[i],
                    config.size(), activeEnd, metrics, failure);
            }
            while (System.nanoTime() < activeEnd) {
                Throwable error = failure.get();
                if (error != null) {
                    throw new IllegalStateException(
                        "spot reqrep callback failed", error);
                }
                boolean sendProgress = false;
                boolean hasWaitingReply = false;
                for (int i = 0; i < activeClients; i++) {
                    if (waitingReply[i].get()) {
                        hasWaitingReply = true;
                        continue;
                    }
                    PerfUtil.resetAndWritePayload(payloads[i], config.size(),
                        (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime());
                    waitingReply[i].set(true);
                    if (submitRequest(requesters.get(i), payloads[i],
                            requestTimeout, callbacks[i])) {
                        sendProgress = true;
                    } else {
                        waitingReply[i].set(false);
                    }
                }
                if (!sendProgress && hasWaitingReply) {
                    if (!waitForCompletion(completionPoller, completionEvents,
                            activeTimer, true)) {
                        break;
                    }
                } else if (sendProgress) {
                    waitForCompletion(completionPoller, completionEvents,
                        activeTimer, false);
                }
            }
            waitForOutstandingCallbacks(waitingReply, failure,
                completionPoller, completionEvents, activeTimer);
        } finally {
            Message.closeAll(List.of(payloads));
        }
    }

    private static boolean waitForCompletion(Poller poller, PollEvents events,
                                             Timer activeTimer,
                                             boolean block) {
        int count = poller.wait(events,
            block ? Duration.ofMillis(-1) : Duration.ZERO);
        for (int i = 0; i < count; i++) {
            if (events.slot(i) == ACTIVE_DEADLINE_SLOT) {
                activeTimer.recv();
                return false;
            }
        }
        return true;
    }

    private static boolean submitRequest(Spot requester,
                                         Message payload,
                                         Duration requestTimeout,
                                         RequestCallback callback) {
        try {
            return requester.requestToSpot(SERVER_NODE_RID, SERVER_SPOT_RID)
                .message(payload)
                .flags(SendFlags.DONT_WAIT)
                .timeout(requestTimeout)
                .submit(callback);
        } catch (SubmitException ex) {
            if (isTransientSubmit(ex)) {
                return false;
            }
            throw ex;
        }
    }

    private static final class RequestMetricsCallback implements RequestCallback {
        private final AtomicBoolean waitingReply;
        private final int expectedSize;
        private final long activeEnd;
        private final PerfUtil.Metrics metrics;
        private final AtomicReference<Throwable> failure;

        private RequestMetricsCallback(AtomicBoolean waitingReply,
                                       int expectedSize,
                                       long activeEnd,
                                       PerfUtil.Metrics metrics,
                                       AtomicReference<Throwable> failure) {
            this.waitingReply = waitingReply;
            this.expectedSize = expectedSize;
            this.activeEnd = activeEnd;
            this.metrics = metrics;
            this.failure = failure;
        }

        @Override
        public void onComplete(RequestResult result, List<Message> parts) {
            try {
                if (result == RequestResult.OK
                    && parts != null
                    && !parts.isEmpty()
                    && System.nanoTime() < activeEnd) {
                    PerfUtil.Header reply = PerfUtil.decodeHeader(
                        parts.get(0), expectedSize);
                    if (reply.phase() == PerfUtil.PHASE_ACTIVE) {
                        metrics.recordNanos(reply.latencyNanos() / 2L);
                    }
                }
            } catch (Throwable ex) {
                failure.compareAndSet(null, ex);
            } finally {
                closeReplyParts(parts);
                waitingReply.set(false);
            }
        }
    }

    private static void closeReplyParts(List<Message> parts) {
        if (parts == null || parts.isEmpty()) {
            return;
        }
        if (parts.size() == 1) {
            parts.get(0).close();
            return;
        }
        Message.closeAll(parts);
    }

    private static void waitForOutstandingCallbacks(AtomicBoolean[] waitingReply,
                                                    AtomicReference<Throwable> failure,
                                                    Poller completionPoller,
                                                    PollEvents completionEvents,
                                                    Timer activeTimer) {
        long deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(500);
        activeTimer.start(Duration.ofMillis(500), 1);
        while (System.nanoTime() < deadline) {
            Throwable error = failure.get();
            if (error != null) {
                throw new IllegalStateException("spot reqrep callback failed",
                    error);
            }
            boolean hasWaitingReply = false;
            for (AtomicBoolean waiting : waitingReply) {
                if (waiting.get()) {
                    hasWaitingReply = true;
                    break;
                }
            }
            if (!hasWaitingReply) {
                return;
            }
            if (!waitForCompletion(completionPoller, completionEvents,
                    activeTimer, true)) {
                return;
            }
        }
    }

    private static int activeSpotSlotLimit(int clients, int msgSize) {
        if (msgSize >= 131_072)
            return Math.min(clients, 8);
        if (msgSize >= 65_536)
            return Math.min(clients, 32);
        return clients;
    }

    private static boolean isTransientSubmit(SubmitException ex) {
        SubmitResult result = ex.getResult();
        return result == SubmitResult.BACKPRESSURED
            || result == SubmitResult.NOT_CONNECTED;
    }

    private static boolean recvRoutedNoWait(Spot spot, Received received) {
        try {
            return spot.recvRouted(received, RecvFlags.DONT_WAIT);
        } catch (RecvException ex) {
            if (ex.getResult() == RecvResult.NO_DATA
                || ex.getResult() == RecvResult.BUSY) {
                return false;
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
