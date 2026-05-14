/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.PollEvent;
import systems.zlink.PollEventFlag;
import systems.zlink.Poller;
import systems.zlink.RecvException;
import systems.zlink.RecvFlags;
import systems.zlink.RecvResult;
import systems.zlink.Received;
import systems.zlink.RoutingId;
import systems.zlink.SendFlags;
import systems.zlink.SubmitException;
import systems.zlink.SubmitResult;
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
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
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
                if (info != null
                    && info.event()
                    == systems.zlink.SpotDispatchEvent.ROUTED_READABLE) {
                    try {
                        drainServer(spot, config.clients(), stopSeen,
                            stopped);
                    } catch (Throwable ex) {
                        failure.compareAndSet(null, ex);
                        stopped.countDown();
                    }
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
                    try (Message active = PerfUtil.payloadTemplate(config.size());
                         Poller poller = new Poller()) {
                        poller.add(spot, PollEventFlag.POLLIN,
                            PollEventFlag.POLLOUT);
                        while (System.nanoTime() < activeEnd) {
                            PerfUtil.resetAndWritePayload(active, config.size(),
                                (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime());
                            sendToServerWhenWritable(spot, poller, active);
                            PerfUtil.Header reply = recvExpected(spot,
                                poller, config.size(), activeEnd);
                            if (reply != null
                                && reply.phase() == PerfUtil.PHASE_ACTIVE
                                && System.nanoTime() < activeEnd) {
                                metrics.recordNanos(reply.latencyNanos() / 2L);
                            }
                        }
                    }
                    try (Message stop = PerfStopToken.newMessage()) {
                        sendToServer(spot, stop, SendFlags.NONE);
                    }
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

    private static void sendToServerWhenWritable(Spot spot,
                                                 Poller poller,
                                                 Message message) {
        for (;;) {
            try {
                sendToServer(spot, message, SendFlags.DONT_WAIT);
                return;
            } catch (SubmitException ex) {
                if (ex.getResult() != SubmitResult.BACKPRESSURED) {
                    throw ex;
                }
                waitFor(poller, PollEventFlag.POLLOUT);
            }
        }
    }

    private static PerfUtil.Header recvExpected(Spot spot, Poller poller, int size,
                                                long deadlineNs) {
        for (;;) {
            if (System.nanoTime() >= deadlineNs) {
                return null;
            }
            waitFor(poller, PollEventFlag.POLLIN);
            Received received = recvRoutedNoWait(spot);
            if (received == null) {
                continue;
            }
            try (received) {
                if (PerfStopToken.isStopTokenMessage(received.firstPart())) {
                    continue;
                }
                return PerfUtil.decodeHeader(received.firstPart(), size);
            }
        }
    }

    private static void waitFor(Poller poller, PollEventFlag expected) {
        List<PollEvent> events = new ArrayList<>(1);
        for (;;) {
            poller.wait(events, Duration.ofMillis(-1));
            for (PollEvent event : events) {
                if (event.revents().contains(expected)) {
                    return;
                }
            }
        }
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
                config.size(), config.clients(),
                config.connectReadyTimeoutMs());
            for (String endpoint : ready.dataEndpoints()) {
                dataNode.connectPeer(normalizeClientEndpoint(endpoint,
                    config.transport()));
            }
            settleAfterReady();
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
