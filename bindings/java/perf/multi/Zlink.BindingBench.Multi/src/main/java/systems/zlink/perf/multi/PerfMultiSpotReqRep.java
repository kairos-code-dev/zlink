/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.RecvException;
import systems.zlink.RecvFlags;
import systems.zlink.RecvResult;
import systems.zlink.Received;
import systems.zlink.RoutingId;
import systems.zlink.SendFlags;
import systems.zlink.perf.PerfControl;
import systems.zlink.perf.PerfStopToken;
import systems.zlink.perf.PerfUtil;
import systems.zlink.service.spot.Spot;
import systems.zlink.service.spot.SpotNode;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
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
                if (info != null
                    && info.event()
                    == systems.zlink.SpotDispatchEvent.ROUTED_READABLE) {
                    try {
                        drainServer(replier, config.clients(), stopSeen,
                            stopRequested);
                    } catch (Throwable ex) {
                        failure.compareAndSet(null, ex);
                        stopRequested.set(true);
                    }
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
                waitForPeerConnected(node, config.connectReadyTimeoutMs());
                settleAfterReady();
                PerfUtil.recalculateAutoHwm(ctx);
                PerfUtil.printMultiSpotNodeAutoHwm(config, node, "client");
                control.publishDataEndpoint(clientDataEndpoint);
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
                    received.send().message(reply).submit();
                }
                if (stop && stopSeen.incrementAndGet() >= expectedStops) {
                    stopRequested.set(true);
                    return progressed;
                }
            }
        }
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
                    try (Message active = PerfUtil.payloadTemplate(config.size())) {
                        while (System.nanoTime() < activeEnd) {
                            PerfUtil.resetAndWritePayload(active, config.size(),
                                (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime());
                            sendToServer(requester, active, SendFlags.NONE);
                            PerfUtil.Header reply = recvExpected(requester,
                                config.size(), activeEnd);
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

    private static PerfUtil.Header recvExpected(Spot requester,
                                                int expectedSize,
                                                long deadlineNs) {
        while (System.nanoTime() < deadlineNs) {
            Received received = recvRoutedNoWait(requester);
            if (received == null) {
                sleepQuietly(1);
                continue;
            }
            try (received) {
                if (PerfStopToken.isStopTokenMessage(received.firstPart())) {
                    continue;
                }
                return PerfUtil.decodeHeader(received.firstPart(), expectedSize);
            }
        }
        return null;
    }

    private static Received recvRoutedNoWait(Spot spot) {
        try {
            return spot.recvRouted(RecvFlags.DONT_WAIT);
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
                config.size(), config.clients(), config.connectReadyTimeoutMs());
            for (String endpoint : ready.dataEndpoints()) {
                dataNode.connectPeer(normalizeClientEndpoint(endpoint,
                    config.transport()));
            }
            waitForPeerConnected(dataNode, config.connectReadyTimeoutMs());
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

    private static void waitForPeerConnected(SpotNode node, int timeoutMs) {
        long deadline = System.nanoTime()
            + Duration.ofMillis(Math.max(1, timeoutMs)).toNanos();
        while (System.nanoTime() < deadline) {
            if (node.statusSnapshot().connectedPeerCount() > 0) {
                return;
            }
            sleepQuietly(10);
        }
        throw new IllegalStateException("spot reqrep peer connect timed out");
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
