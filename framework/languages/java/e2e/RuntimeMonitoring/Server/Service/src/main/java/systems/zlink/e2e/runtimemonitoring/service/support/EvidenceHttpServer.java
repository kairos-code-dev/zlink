package systems.zlink.e2e.runtimemonitoring.service.support;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.SmartLifecycle;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.runtimemonitoring.service.handlers.TriggeredMonitoringSpot;
import systems.zlink.e2e.runtimemonitoring.service.handlers.MonitoringSpot;
import systems.zlink.e2e.runtimemonitoring.service.handlers.MulticastGate;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkRouteMeshRuntimeOptions;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.monitoring.ZLinkMeshNodeSnapshot;
import systems.zlink.framework.monitoring.ZLinkRouteMeshRuntime;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;

public final class EvidenceHttpServer implements SmartLifecycle {
    private final EvidenceState state;
    private final ObjectMapper json;
    private final ZLinkChannelRuntimeOptions runtimeOptions;
    private final ZLinkRouteMeshRuntimeOptions meshRuntimeOptions;
    private final ZLinkRouteClient routeClient;
    private final ObjectProvider<ZLinkSpotPublisherClient> publisher;
    private final MulticastGate multicastGate;
    private final ObjectProvider<ZLinkRouteMeshRuntime> meshRuntime;
    private final ObserverIsolationProbe observerIsolation;
    private final ObjectProvider<ZLinkSpotManager> spots;
    private final ConfigurableApplicationContext applicationContext;
    private final String endpoint;
    private HttpServer server;
    private boolean running;

    public EvidenceHttpServer(
        EvidenceState state,
        ObjectMapper json,
        ZLinkChannelRuntimeOptions runtimeOptions,
        ZLinkRouteMeshRuntimeOptions meshRuntimeOptions,
        ZLinkRouteClient routeClient,
        ObjectProvider<ZLinkSpotPublisherClient> publisher,
        MulticastGate multicastGate,
        ObjectProvider<ZLinkRouteMeshRuntime> meshRuntime,
        ObserverIsolationProbe observerIsolation,
        ObjectProvider<ZLinkSpotManager> spots,
        ConfigurableApplicationContext applicationContext,
        String endpoint) {
        this.state = state;
        this.json = json;
        this.runtimeOptions = runtimeOptions;
        this.meshRuntimeOptions = meshRuntimeOptions;
        this.routeClient = routeClient;
        this.publisher = publisher;
        this.multicastGate = multicastGate;
        this.meshRuntime = meshRuntime;
        this.observerIsolation = observerIsolation;
        this.spots = spots;
        this.applicationContext = applicationContext;
        this.endpoint = endpoint;
    }

    @Override
    public void start() {
        if (endpoint == null || endpoint.isBlank()) {
            return;
        }
        try {
            URI uri = URI.create(endpoint);
            server = HttpServer.create(new InetSocketAddress(uri.getHost(), uri.getPort()), 0);
            server.createContext("/health", exchange -> write(exchange, "ok\n"));
            server.createContext("/evidence", exchange -> write(
                exchange,
                json.writeValueAsString(state.snapshot())));
            server.createContext("/runtime/snapshot", exchange -> write(
                exchange,
                json.writeValueAsString(runtimeSnapshot())));
            server.createContext("/runtime/observer/start", exchange -> write(
                exchange,
                json.writeValueAsString(observerIsolation.start(meshRuntime.getObject()))));
            server.createContext("/runtime/observer/status", exchange -> write(
                exchange,
                json.writeValueAsString(observerIsolation.status())));
            server.createContext("/runtime/observer/release", exchange -> write(
                exchange,
                json.writeValueAsString(observerIsolation.release())));
            server.createContext("/runtime/weight/zero", exchange -> {
                setMeshWeight(0);
                write(exchange, json.writeValueAsString(new AdminResult("weight-updated", 0)));
            });
            server.createContext("/runtime/weight/restore", exchange -> {
                setMeshWeight(100);
                write(exchange, json.writeValueAsString(new AdminResult("weight-updated", 100)));
            });
            server.createContext("/runtime/request", exchange -> {
                Contracts.WorkReq request =
                    json.readValue(exchange.getRequestBody(), Contracts.WorkReq.class);
                Contracts.WorkRes response = routeClient.requestToChannel(
                        Contracts.SPOT_MESH,
                        Contracts.SPOT_CHANNEL,
                        request)
                    .timeout(java.time.Duration.ofSeconds(5))
                    .submit(Contracts.WorkRes.class)
                    .toCompletableFuture()
                    .join();
                write(exchange, json.writeValueAsString(response));
            });
            server.createContext("/runtime/multicast/publish", exchange -> {
                Contracts.PublishCommand command =
                    json.readValue(exchange.getRequestBody(), Contracts.PublishCommand.class);
                systems.zlink.framework.channels.ZLinkPublishResult result = null;
                int attempts = 0;
                long snapshotRemote = 0;
                long admittedRemote = 0;
                long droppedRemote = 0;
                long snapshotLocal = 0;
                long admittedLocal = 0;
                long droppedLocal = 0;
                for (; attempts < Math.max(1, command.count()); attempts++) {
                    result = publisher.getObject().publish(
                            Contracts.SPOT_MESH,
                            Contracts.SPOT_CHANNEL,
                            command.topic(),
                            new Contracts.MulticastProbe(command.value() + "-" + attempts))
                        .submit()
                        .toCompletableFuture()
                        .join();
                    var attemptDetail = result.detail();
                    snapshotRemote = Math.max(
                        snapshotRemote, attemptDetail.snapshotRemoteNodeCount());
                    admittedRemote = Math.max(
                        admittedRemote, attemptDetail.admittedRemoteNodeCount());
                    droppedRemote += attemptDetail.droppedRemoteNodeCount();
                    snapshotLocal = Math.max(
                        snapshotLocal, attemptDetail.snapshotLocalSpotCount());
                    admittedLocal = Math.max(
                        admittedLocal, attemptDetail.admittedLocalSpotCount());
                    droppedLocal += attemptDetail.droppedLocalSpotCount();
                    if (result.status()
                        != systems.zlink.framework.channels.ZLinkSubmitStatus.SUBMITTED
                        || attemptDetail.droppedRemoteNodeCount() > 0
                        || (command.stopOnLocalDrop()
                            && attemptDetail.droppedLocalSpotCount() > 0)) {
                        attempts++;
                        break;
                    }
                }
                java.util.Objects.requireNonNull(result);
                write(exchange, json.writeValueAsString(new Contracts.PublishOutcome(
                    result.status().name(),
                    snapshotRemote,
                    admittedRemote,
                    droppedRemote,
                    snapshotLocal,
                    admittedLocal,
                    droppedLocal,
                    attempts)));
            });
            server.createContext("/runtime/multicast/block", exchange -> {
                String rid = requiredQuery(exchange.getRequestURI(), "rid");
                multicastGate.block(rid);
                write(exchange, json.writeValueAsString(new AdminResult("blocked", -1)));
            });
            server.createContext("/runtime/multicast/release", exchange -> {
                String rid = requiredQuery(exchange.getRequestURI(), "rid");
                multicastGate.release(rid);
                write(exchange, json.writeValueAsString(new AdminResult("released", -1)));
            });
            server.createContext("/runtime/multicast/create", exchange -> {
                String rid = requiredQuery(exchange.getRequestURI(), "rid");
                spots.getObject().getOrCreate(
                    MonitoringSpot.class,
                    RoutingId.from(rid),
                    ZLinkMessage.of(rid)).toCompletableFuture().join();
                write(exchange, json.writeValueAsString(new AdminResult("created", -1)));
            });
            server.createContext("/admin/drain", exchange -> {
                setWeight(0, "drain");
                write(exchange, json.writeValueAsString(new AdminResult("drained", 0)));
            });
            server.createContext("/admin/restore", exchange -> {
                setWeight(100, "restore");
                write(exchange, json.writeValueAsString(new AdminResult("restored", 100)));
            });
            server.createContext("/admin/create-subject-spot", exchange -> {
                try {
                    ZLinkSpotManager manager = spots.getIfAvailable();
                    if (manager == null) {
                        throw new IllegalStateException("spot manager is not configured");
                    }
                    manager.getOrCreate(
                        TriggeredMonitoringSpot.class,
                        RoutingId.from("monitoring-subject-trigger"),
                        ZLinkMessage.of("monitoring-subject-trigger"))
                        .toCompletableFuture()
                        .join();
                    write(exchange, json.writeValueAsString(new AdminResult("subject-created", -1)));
                } catch (RuntimeException error) {
                    Throwable cause = error.getCause() == null ? error : error.getCause();
                    write(exchange, 500, cause.getClass().getName() + ": " + cause.getMessage());
                }
            });
            server.createContext("/shutdown", exchange -> {
                write(exchange, json.writeValueAsString(new AdminResult("stopping", -1)));
                Thread shutdown = new Thread(applicationContext::close, "runtime-monitoring-shutdown");
                shutdown.setDaemon(false);
                shutdown.start();
            });
            server.start();
            running = true;
        } catch (Exception error) {
            throw new IllegalStateException("failed to start evidence endpoint " + endpoint, error);
        }
    }

    private void setWeight(int weight, String action) {
        runtimeOptions
            .clientServerChannel(Contracts.CHANNEL)
            .configureServerSocket()
            .weight(weight);
        state.record("admin", state.rid(), action, "weight=" + weight);
    }

    private void setMeshWeight(int weight) {
        meshRuntimeOptions
            .channel(Contracts.SPOT_MESH, Contracts.SPOT_CHANNEL)
            .weight(weight);
    }

    private Contracts.RuntimeSnapshot runtimeSnapshot() {
        ZLinkRouteMeshRuntime runtime = meshRuntime.getIfAvailable();
        if (runtime == null) {
            throw new IllegalStateException("RouteMesh runtime is not configured");
        }
        ZLinkMeshNodeSnapshot snapshot = runtime.snapshot(Contracts.SPOT_MESH);
        return new Contracts.RuntimeSnapshot(
            snapshot.meshName(),
            snapshot.rid().toHex(),
            snapshot.lifecycleGeneration(),
            snapshot.descriptorRevision(),
            snapshot.endpoint(),
            snapshot.state().name(),
            snapshot.sequence(),
            snapshot.observedAt().toString(),
            snapshot.descriptorSources(),
            snapshot.peers().stream().map(peer -> new Contracts.RuntimePeer(
                peer.rid().toHex(),
                peer.lifecycleGeneration(),
                peer.descriptorRevision(),
                peer.endpoint(),
                peer.admissionState(),
                peer.ready(),
                peer.channelNames(),
                peer.lastFailure().orElse(""))).toList(),
            snapshot.channels().stream().map(channel -> new Contracts.RuntimeChannel(
                channel.channelName(),
                channel.localWeight(),
                channel.readyMemberCount(),
                channel.selectable())).toList(),
            snapshot.multicast().submitted(),
            snapshot.multicast().backpressured(),
            snapshot.multicast().dropped(),
            snapshot.claims().applicationActive(),
            snapshot.claims().pendingApplicationWork(),
            snapshot.claims().infrastructureActive(),
            snapshot.claims().pendingInfrastructureWork(),
            snapshot.location().state(),
            snapshot.location().lastSuccessAt().map(Object::toString).orElse(""),
            snapshot.location().lastFailureAt().map(Object::toString).orElse(""),
            snapshot.drain().state().name(),
            snapshot.drain().workSealed(),
            snapshot.drain().pendingRequestCount(),
            snapshot.drain().pendingTransferCount(),
            snapshot.drain().pendingStreamBarrierCount());
    }

    private record AdminResult(String status, int weight) {
    }

    private static String requiredQuery(URI uri, String name) {
        String query = uri.getRawQuery();
        if (query != null) {
            for (String part : query.split("&")) {
                String[] pair = part.split("=", 2);
                if (pair.length == 2 && pair[0].equals(name) && !pair[1].isBlank()) {
                    return java.net.URLDecoder.decode(
                        pair[1], StandardCharsets.UTF_8);
                }
            }
        }
        throw new IllegalArgumentException("missing query parameter: " + name);
    }

    private static void write(
        com.sun.net.httpserver.HttpExchange exchange,
        String value) throws java.io.IOException {
        write(exchange, 200, value);
    }

    private static void write(
        com.sun.net.httpserver.HttpExchange exchange,
        int status,
        String value) throws java.io.IOException {
        byte[] body = value.getBytes(StandardCharsets.UTF_8);
        exchange.getResponseHeaders().add("Content-Type", "application/json");
        exchange.sendResponseHeaders(status, body.length);
        exchange.getResponseBody().write(body);
        exchange.close();
    }

    @Override
    public void stop() {
        if (server != null) {
            server.stop(0);
            server = null;
        }
        running = false;
    }

    @Override
    public boolean isRunning() {
        return running;
    }
}
