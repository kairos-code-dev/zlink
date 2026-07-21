/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.Dispatch;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.OperationId;
import systems.zlink.contracts.service.spot.OperationKind;
import systems.zlink.contracts.service.spot.RecordKind;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.perf.PerfControl;
import systems.zlink.perf.PerfMeshDispatch;
import systems.zlink.perf.PerfUtil;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

final class PerfMultiSpotReqRep {
    private static final String CHANNEL = "bench-reqrep";
    private static final RoutingId SERVER_NODE_RID =
        routingId("SPOT-REQREP-SERVER-NODE");
    private static final RoutingId SERVER_SPOT_RID =
        routingId("SPOT-REQREP-SERVER-SPOT");

    private PerfMultiSpotReqRep() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        if (secureTransport(config.transport())) {
            return PerfUtil.Result.unsupported(
                "MeshNode benchmark trust profiles are not configured", config);
        }
        String controlEndpoint = derivedEndpoint(config.endpoint(), 1);
        try (Context ctx = PerfUtil.newContext(config);
             MeshNode node = ctx.createMeshNode();
             PerfSpotDirectControl control = PerfSpotDirectControl.bind(
                 ctx, config, controlEndpoint, "reqrep-server")) {
            node.setRoutingId(SERVER_NODE_RID);
            node.setBind(config.endpoint());
            node.addChannel(CHANNEL);
            node.start();
            try (Spot replier = node.getOrCreateSpot(SERVER_SPOT_RID).spot();
                 PerfMeshDispatch dispatch = new PerfMeshDispatch(node, 64)) {
                PerfControl.emitReady(config.endpoint());
                PerfControl.emitControlReady(controlEndpoint);
                awaitDirectControlStart(control, node, config,
                    "mesh reqrep server", spotServerReadyTimeoutMs(config));
                PerfUtil.recalculateAutoHwm(ctx);
                long activeEnd = System.nanoTime()
                    + Duration.ofSeconds(config.durationSeconds()).toNanos();
                while (System.nanoTime() < activeEnd) {
                    int count = dispatch.drain((owner, record, parts) -> {
                        if (record.kind() != RecordKind.SPOT_REQUEST
                            || record.replyToken() == null
                            || parts.isEmpty()) {
                            return;
                        }
                        try (Message reply = Message.from(parts.get(0))) {
                            Dispatch.reply(
                                record.replyToken(), List.of(reply), SendFlags.NONE);
                        }
                    });
                    if (count == 0) {
                        Thread.onSpinWait();
                    }
                }
                return PerfUtil.Result.silent(config);
            }
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        if (secureTransport(config.transport())) {
            return PerfUtil.Result.unsupported(
                "MeshNode benchmark trust profiles are not configured", config);
        }
        String endpoint = normalizeClientEndpoint(config.endpoint(), config.transport());
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
             MeshNode node = ctx.createMeshNode();
             PerfSpotDirectControl control = PerfSpotDirectControl.bind(
                 ctx, config, clientControlEndpoint, "reqrep-client")) {
            node.setRoutingId(routingId("SPOT-REQREP-CLIENT-NODE"));
            node.setBind(clientDataEndpoint);
            node.addChannel(CHANNEL);
            node.start();
            node.connectPeer(endpoint, SERVER_NODE_RID);
            List<Spot> requesters = new ArrayList<>(config.clients());
            try (PerfMeshDispatch dispatch =
                     new PerfMeshDispatch(node, Math.max(16, config.clients()))) {
                for (int i = 0; i < config.clients(); i++) {
                    requesters.add(node.createSpot());
                }
                control.connectPeer(serverControlEndpoint);
                PerfControl.emitClientControlEndpoint(clientControlEndpoint);
                PerfControl.awaitControlConnected(clientControlEndpoint,
                    "mesh reqrep client");
                waitForConnectedPeers(node, 1, config.connectReadyTimeoutMs(),
                    "mesh reqrep client data link");
                control.publishDataEndpoint(clientDataEndpoint);
                control.publishConnected();
                control.publishReadyCount(config.size(), requesters.size());
                PerfControl.emitClientReady(config.size());
                PerfControl.awaitStart(config.size(), "mesh reqrep client");
                control.waitStart(config.size(), config.connectReadyTimeoutMs());
                runRequests(node, dispatch, requesters, config, metrics);
                return metrics.finishMulti(config);
            } finally {
                requesters.forEach(Spot::close);
            }
        }
    }

    private static void runRequests(
        MeshNode node,
        PerfMeshDispatch dispatch,
        List<Spot> requesters,
        PerfUtil.Config config,
        PerfUtil.Metrics metrics) {
        long activeEnd = System.nanoTime()
            + Duration.ofSeconds(config.durationSeconds()).toNanos();
        Map<OperationId, Integer> pending = new HashMap<>();
        boolean[] busy = new boolean[requesters.size()];
        while (System.nanoTime() < activeEnd || !pending.isEmpty()) {
            if (System.nanoTime() < activeEnd) {
                for (int i = 0; i < requesters.size(); i++) {
                    if (busy[i]) {
                        continue;
                    }
                    try (Message request = PerfUtil.payload(
                        config.size(), (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                        OperationId operation = requesters.get(i).requestToSpot(
                            SERVER_NODE_RID,
                            SERVER_SPOT_RID,
                            1L,
                            List.of(request),
                            SendFlags.DONT_WAIT,
                            Duration.ofSeconds(5));
                        pending.put(operation, i);
                        busy[i] = true;
                    } catch (ZlinkSubmitException error) {
                        if (!transientSubmit(error)) {
                            throw error;
                        }
                    }
                }
            }
            int progressed = dispatch.drain((owner, record, parts) -> {
                if (record.kind() != RecordKind.COMPLETION
                    || record.operationKind() != OperationKind.SPOT_REQUEST) {
                    return;
                }
                Integer slot = pending.remove(record.operationId());
                if (slot != null) {
                    busy[slot] = false;
                }
                if (record.terminalResult() == 0 && !parts.isEmpty()
                    && PerfUtil.recordActiveLatency(
                        metrics, parts.get(0), config.size(), true)) {
                    metrics.recordEvent();
                }
            });
            if (progressed == 0) {
                Thread.onSpinWait();
            }
            if (System.nanoTime() >= activeEnd && pending.isEmpty()) {
                break;
            }
        }
    }

    private static boolean transientSubmit(ZlinkSubmitException error) {
        return error.getResult() == SubmitResult.BACKPRESSURED
            || error.getResult() == SubmitResult.NOT_CONNECTED;
    }

    private static void awaitDirectControlStart(
        PerfSpotDirectControl control,
        MeshNode dataNode,
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
                endpoint -> dataNode.connectPeer(normalizeClientEndpoint(
                    endpoint, config.transport())));
            if (ready.dataEndpoints().isEmpty()) {
                throw new IllegalStateException(label + " missing data endpoint");
            }
            waitForConnectedPeers(
                dataNode, ready.dataEndpoints().size(), timeoutMs, label + " data link");
            while ((line = reader.readLine()) != null) {
                if (expectedStart.equals(line)) {
                    control.publishStart(config.size());
                    return;
                }
            }
        } catch (java.io.IOException error) {
            throw new IllegalStateException(label + " control read failed", error);
        }
        throw new IllegalStateException(label + " missing " + expectedStart);
    }

    private static void waitForConnectedPeers(
        MeshNode node,
        int expectedPeers,
        int timeoutMs,
        String label) {
        long deadline = System.nanoTime()
            + Duration.ofMillis(Math.max(1, timeoutMs)).toNanos();
        while (System.nanoTime() < deadline) {
            if (node.status().admittedPeerCount() >= expectedPeers) {
                return;
            }
            sleepQuietly(10);
        }
        throw new IllegalStateException(label + " connected peer timeout");
    }

    private static int spotServerReadyTimeoutMs(PerfUtil.Config config) {
        int connectTimeoutMs = Math.max(1, config.connectReadyTimeoutMs());
        return Math.max(connectTimeoutMs, Math.max(1000, connectTimeoutMs * 6));
    }

    private static boolean secureTransport(String transport) {
        return "tls".equals(transport) || "wss".equals(transport);
    }

    private static void sleepQuietly(int millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("mesh reqrep sleep interrupted", error);
        }
    }

    private static String normalizeClientEndpoint(String endpoint, String transport) {
        return endpoint;
    }

    private static String derivedEndpoint(String endpoint, int portOffset) {
        int schemeSep = endpoint.indexOf("://");
        int colon = endpoint.lastIndexOf(':');
        if (schemeSep <= 0 || colon <= schemeSep + 2
            || colon == endpoint.length() - 1) {
            throw new IllegalArgumentException("cannot derive endpoint from: " + endpoint);
        }
        int port = Integer.parseInt(endpoint.substring(colon + 1));
        return endpoint.substring(0, colon + 1) + (port + portOffset);
    }

    private static RoutingId routingId(String value) {
        return RoutingId.from(value.getBytes(StandardCharsets.UTF_8));
    }
}
