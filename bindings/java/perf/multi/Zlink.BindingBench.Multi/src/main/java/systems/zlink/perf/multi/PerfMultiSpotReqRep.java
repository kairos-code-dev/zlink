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
import systems.zlink.contracts.SubmitException;
import systems.zlink.contracts.SubmitResult;
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
        try (Context ctx = PerfUtil.newContext(config);
             SpotNode node = new SpotNode(ctx);
             Spot replier = node.createSpot();
             PerfSpotDirectControl control = PerfSpotDirectControl.bind(
                 ctx, config, controlEndpoint, "reqrep-server")) {
            node.setRoutingId(SERVER_NODE_RID);
            replier.setRoutingId(SERVER_SPOT_RID);
            PerfUtil.configureServerTls(node, config.transport());
            PerfUtil.configureClientTls(node, config.transport());
            node.bind(config.endpoint());
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
                "spot reqrep server");
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
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);

        try (Context ctx = PerfUtil.newContext(config);
             SpotNode node = new SpotNode(ctx);
             PerfSpotDirectControl control = PerfSpotDirectControl.bind(
                 ctx, config, clientControlEndpoint, "reqrep-client")) {
            node.setRoutingId(routingId("SPOT-REQREP-CLIENT-NODE"));
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

    private static boolean drainServer(Spot replier, long activeEnd) {
        boolean progressed = false;
        for (;;) {
            if (System.nanoTime() >= activeEnd) {
                return progressed;
            }
            Received received = recvRoutedNoWait(replier);
            if (received == null) {
                return progressed;
            }
            progressed = true;
            try (received) {
                try (Message reply = received.firstPart().move()) {
                    try {
                        received.reply().message(reply).submit();
                    } catch (SubmitException ex) {
                        if (!isTransientSubmit(ex)) {
                            throw ex;
                        }
                    }
                }
            }
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
        Poller completionPoller = null;
        try {
            List<PollEvent> completionEvents = new ArrayList<>(activeClients);
            if (config.size() >= 65_536) {
                completionPoller = new Poller();
                for (int i = 0; i < activeClients; i++) {
                    completionPoller.add(requesters.get(i), Integer.valueOf(i),
                        PollEventFlag.POLLCOMPLETION);
                }
            }
            long activeEnd = System.nanoTime()
                + config.durationSeconds() * 1_000_000_000L;
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
                            waitingReply[i], config.size(), activeEnd,
                            metrics, failure)) {
                        sendProgress = true;
                    } else {
                        waitingReply[i].set(false);
                    }
                }
                if (!sendProgress && hasWaitingReply) {
                    if (completionPoller != null) {
                        completionPoller.wait(completionEvents,
                            Duration.ofMillis(1));
                    } else {
                        java.util.concurrent.locks.LockSupport.parkNanos(
                            100_000L);
                    }
                } else if (completionPoller != null) {
                    completionPoller.wait(completionEvents,
                        Duration.ZERO);
                }
            }
            waitForOutstandingCallbacks(waitingReply, failure);
        } finally {
            if (completionPoller != null) {
                completionPoller.close();
            }
            Message.closeAll(List.of(payloads));
        }
    }

    private static boolean submitRequest(Spot requester,
                                         Message payload,
                                         AtomicBoolean waitingReply,
                                         int expectedSize,
                                         long activeEnd,
                                         PerfUtil.Metrics metrics,
                                         AtomicReference<Throwable> failure) {
        long remainingNs = activeEnd - System.nanoTime();
        if (remainingNs <= 0L) {
            return false;
        }
        try {
            return requester.requestToSpot(SERVER_NODE_RID, SERVER_SPOT_RID)
              .message(payload)
              .timeout(Duration.ofNanos(remainingNs))
              .flags(SendFlags.DONT_WAIT)
              .submit((result, parts) -> {
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
                      Message.closeAll(parts);
                      waitingReply.set(false);
                  }
              });
        } catch (SubmitException ex) {
            if (isTransientSubmit(ex)) {
                return false;
            }
            throw ex;
        }
    }

    private static void waitForOutstandingCallbacks(AtomicBoolean[] waitingReply,
                                                    AtomicReference<Throwable> failure) {
        long deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(500);
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
            java.util.concurrent.locks.LockSupport.parkNanos(100_000L);
        }
    }

    private static int activeSpotSlotLimit(int clients, int msgSize) {
        int hwmSlots = Math.max(1, 1_048_576 / Math.max(1, msgSize));
        if (msgSize >= 131_072)
            return Math.min(clients, Math.min(8, hwmSlots));
        if (msgSize >= 65_536)
            return Math.min(clients, Math.min(32, hwmSlots));
        return clients;
    }

    private static boolean isTransientSubmit(SubmitException ex) {
        SubmitResult result = ex.getResult();
        return result == SubmitResult.BACKPRESSURED
            || result == SubmitResult.NOT_CONNECTED;
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
