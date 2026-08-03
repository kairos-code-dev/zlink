package systems.zlink.e2e.spotservice.shared;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.util.List;
import org.springframework.context.SmartLifecycle;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locationprovider.ZLinkLocationStore;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.runtime.internal.locations.ZLinkLocationRepository;
import systems.zlink.framework.locations.ZLinkPageRequest;
import systems.zlink.framework.spots.SpotHandle;
import systems.zlink.framework.spots.SpotHandleResolver;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.monitoring.ZLinkRouteMeshRuntime;
import systems.zlink.framework.monitoring.ZLinkPeerState;
import systems.zlink.framework.spots.ZLinkSpotManager;

public final class EvidenceHttpServer implements SmartLifecycle {
    private final ScenarioState state;
    private final ObjectMapper json;
    private final String endpoint;
    private final ZLinkSpotManager spots;
    private final ZLinkRouteClient routes;
    private final SpotHandleResolver spotHandles;
    private final ZLinkRouteMeshRuntime meshRuntime;
    private final ZLinkLocationStore locationStore;
    private HttpServer server;
    private boolean running;

    public EvidenceHttpServer(
        ScenarioState state,
        ObjectMapper json,
        String endpoint,
        ZLinkSpotManager spots,
        ZLinkRouteClient routes,
        SpotHandleResolver spotHandles,
        ZLinkRouteMeshRuntime meshRuntime,
        ZLinkLocationStore locationStore) {
        this.state = state;
        this.json = json;
        this.endpoint = endpoint;
        this.spots = spots;
        this.routes = routes;
        this.spotHandles = spotHandles;
        this.meshRuntime = meshRuntime;
        this.locationStore = locationStore;
    }

    @Override
    public void start() {
        if (endpoint == null || endpoint.isBlank()) {
            return;
        }
        try {
            URI uri = URI.create(endpoint);
            server = HttpServer.create(new InetSocketAddress(uri.getHost(), uri.getPort()), 0);
            server.createContext("/health", exchange -> {
                byte[] body = "ok\n".getBytes(StandardCharsets.UTF_8);
                exchange.sendResponseHeaders(200, body.length);
                exchange.getResponseBody().write(body);
                exchange.close();
            });
            server.createContext("/topology/ready", exchange -> {
                int expected = Integer.parseInt(queryValue(exchange.getRequestURI(), "expected"));
                long ready = meshRuntime.snapshot(Contracts.SPOT_MESH).peers().stream()
                    .filter(peer -> peer.state() == ZLinkPeerState.READY)
                    .count();
                write(exchange, ready >= expected ? 200 : 503, ready + "\n");
            });
            server.createContext("/location/spot-owner", exchange -> {
                String spotRid = queryValue(exchange.getRequestURI(), "spotRid");
                var spot = spots.find(spotRid)
                    .toCompletableFuture().join().orElse(null);
                var owner = spot == null ? null : locationStore.listMeshNodes(
                        Contracts.SPOT_MESH,
                        ZLinkPageRequest.firstPage())
                    .toCompletableFuture().join().items().stream()
                    .filter(node -> node.rid().equals(spot.nodeRid()))
                    .map(node -> node.ownerId())
                    .findFirst().orElse(null);
                write(exchange, owner == null ? 404 : 200,
                    owner == null ? "absent\n" : owner + "\n");
            });
            server.createContext("/evidence", exchange -> {
                byte[] body = json.writeValueAsBytes(state.snapshot());
                exchange.getResponseHeaders().add("Content-Type", "application/json");
                exchange.sendResponseHeaders(200, body.length);
                exchange.getResponseBody().write(body);
                exchange.close();
            });
            server.createContext("/evidence/wait", exchange -> {
                try {
                    Contracts.EvidenceWaitReq request = json.readValue(
                        exchange.getRequestBody(),
                        Contracts.EvidenceWaitReq.class);
                    int timeout = request.timeoutMilliseconds() <= 0
                        ? 10_000
                        : request.timeoutMilliseconds();
                    byte[] body = json.writeValueAsBytes(state.waitFor(request.containsAll(), timeout));
                    exchange.getResponseHeaders().add("Content-Type", "application/json");
                    exchange.sendResponseHeaders(200, body.length);
                    exchange.getResponseBody().write(body);
                    exchange.close();
                } catch (RuntimeException error) {
                    write(exchange, 500, error.getMessage() + "\n");
                }
            });
            server.createContext("/spot/create", exchange -> {
                Contracts.CreateSpotReq request = json.readValue(
                    exchange.getRequestBody(),
                    Contracts.CreateSpotReq.class);
                var result = getOrCreateUserSpot(request.spotRid());
                write(exchange, 200, json.writeValueAsString(new Contracts.CreateSpotRes(
                    result.spotId(),
                    state.nodeRid(),
                    result.state().name())));
            });
            server.createContext("/spot/state/request", exchange -> {
                try {
                    Contracts.SpotStateRouteReq request = json.readValue(
                        exchange.getRequestBody(),
                        Contracts.SpotStateRouteReq.class);
                    Contracts.StateRes result = requestStateWithRetry(request);
                    write(exchange, 200, json.writeValueAsString(result));
                } catch (RuntimeException error) {
                    write(exchange, 500, failureText(error));
                }
            });
            server.createContext("/spot/missing-handler/request", exchange -> {
                Contracts.SpotMissingHandlerReq request = json.readValue(
                    exchange.getRequestBody(),
                    Contracts.SpotMissingHandlerReq.class);
                boolean failed = fails(() -> routes.requestToSpot(
                        Contracts.ROUTE_CHANNEL,
                        spotHandle(request.spotRid()),
                        new Contracts.MissingSpotReq("noop"))
                    .timeout(java.time.Duration.ofSeconds(2))
                    .submit(Contracts.StateRes.class).toCompletableFuture().join());
                write(exchange, 200, json.writeValueAsString(new Contracts.SpotMissingHandlerRes(
                    request.spotRid(),
                    failed,
                    state.snapshot())));
            });
            server.createContext("/spot/missing-handler/command", exchange -> {
                Contracts.SpotMissingCommandReq request = json.readValue(
                    exchange.getRequestBody(),
                    Contracts.SpotMissingCommandReq.class);
                try {
                    routes.sendToSpot(
                            Contracts.ROUTE_CHANNEL,
                            spotHandle(request.spotRid()),
                            new Contracts.MissingSpotMsg(request.marker()))
                        .submit();
                } catch (RuntimeException ignored) {
                    // The assertion below is the contract: a missing send handler is recorded as a drop.
                }
                write(exchange, 200, json.writeValueAsString(new Contracts.SpotMissingCommandRes(
                    request.spotRid(),
                    request.marker(),
                    true,
                    state.snapshot())));
            });
            server.createContext("/spot/stage/request", exchange -> {
                Contracts.SpotStageProbeRouteReq request = json.readValue(
                    exchange.getRequestBody(),
                    Contracts.SpotStageProbeRouteReq.class);
                Contracts.StateRes result = requestStageWithRetry(request);
                write(exchange, 200, json.writeValueAsString(result));
            });
            server.createContext("/spot/stage/timer", exchange -> {
                Contracts.SpotStageTimerRouteReq request = json.readValue(
                    exchange.getRequestBody(),
                    Contracts.SpotStageTimerRouteReq.class);
                Contracts.StageTimerStartRes result = requestStageTimerWithRetry(request);
                write(exchange, 200, json.writeValueAsString(result));
            });
            server.createContext("/admin/close", exchange -> {
                String rid = queryValue(exchange.getRequestURI(), "rid");
                if (rid == null || rid.isBlank()) {
                    write(exchange, 400, "missing rid\n");
                    return;
                }
                boolean closed;
                try {
                    closed = spots.close(RoutingId.from(rid))
                        .toCompletableFuture()
                        .get(5, java.util.concurrent.TimeUnit.SECONDS);
                } catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("spot close interrupted", error);
                } catch (java.util.concurrent.ExecutionException
                         | java.util.concurrent.TimeoutException error) {
                    throw new IllegalStateException("spot close failed", error);
                }
                write(exchange, 200, "{\"closed\":" + closed + "}\n");
            });
            server.createContext("/admin/create-timer", exchange -> {
                String rid = queryValue(exchange.getRequestURI(), "rid");
                if (rid == null || rid.isBlank()) {
                    write(exchange, 400, "missing rid\n");
                    return;
                }
                try {
                    spots.getOrCreate(TimerScenarioSpot.class, RoutingId.from(rid), ZLinkMessage.of("e2e"))
                        .toCompletableFuture()
                        .get(5, java.util.concurrent.TimeUnit.SECONDS);
                } catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("timer spot create interrupted", error);
                } catch (java.util.concurrent.ExecutionException
                         | java.util.concurrent.TimeoutException error) {
                    throw new IllegalStateException("timer spot create failed", error);
                }
                write(exchange, 200, "{\"created\":true}\n");
            });
            server.createContext("/admin/type-mismatch", exchange -> {
                String rid = queryValue(exchange.getRequestURI(), "rid");
                if (rid == null || rid.isBlank()) {
                    write(exchange, 400, "missing rid\n");
                    return;
                }
                try {
                    spots.getOrCreate(MismatchedSpot.class, RoutingId.from(rid))
                        .toCompletableFuture()
                        .get(5, java.util.concurrent.TimeUnit.SECONDS);
                    write(exchange, 500, "{\"mismatch\":false}\n");
                    return;
                } catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("spot type mismatch interrupted", error);
                } catch (java.util.concurrent.ExecutionException error) {
                    state.record("SpotTypeMismatch", rid, error.getCause().getMessage());
                } catch (java.util.concurrent.TimeoutException error) {
                    throw new IllegalStateException("spot type mismatch timed out", error);
                } catch (RuntimeException error) {
                    state.record("SpotTypeMismatch", rid, error.getMessage());
                }
                try {
                    spots.getOrCreate(UserSpot.class, RoutingId.from(rid))
                        .toCompletableFuture()
                        .get(5, java.util.concurrent.TimeUnit.SECONDS);
                    state.record("SpotTypeMismatchStateOk", rid, "existing");
                } catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("spot type mismatch follow-up interrupted", error);
                } catch (java.util.concurrent.ExecutionException
                         | java.util.concurrent.TimeoutException error) {
                    throw new IllegalStateException("spot type mismatch follow-up failed", error);
                }
                write(exchange, 200, "{\"mismatch\":true}\n");
            });
            server.start();
            running = true;
        } catch (Exception error) {
            throw new IllegalStateException("failed to start evidence endpoint " + endpoint, error);
        }
    }

    private systems.zlink.framework.spots.ZLinkSpotCreateResult getOrCreateUserSpot(String spotRid) {
        try {
            return spots.getOrCreate(UserSpot.class, RoutingId.from(spotRid), ZLinkMessage.of("e2e"))
                .toCompletableFuture()
                .get(5, java.util.concurrent.TimeUnit.SECONDS);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("spot create interrupted", error);
        } catch (java.util.concurrent.ExecutionException
                 | java.util.concurrent.TimeoutException error) {
            throw new IllegalStateException("spot create failed", error);
        }
    }

    private Contracts.StateRes requestStateWithRetry(Contracts.SpotStateRouteReq request) {
        long deadline = System.nanoTime() + java.time.Duration.ofSeconds(10).toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                String op = request.op() + "-" + request.delta();
                return routes.requestToSpot(
                        Contracts.ROUTE_CHANNEL,
                        spotHandle(request.spotRid()),
                        new Contracts.StateReq(op))
                    .timeout(java.time.Duration.ofSeconds(2))
                    .submit(Contracts.StateRes.class).toCompletableFuture().join();
            } catch (RuntimeException error) {
                lastFailure = error;
                try {
                    Thread.sleep(100);
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("interrupted while waiting for spot state route", interrupted);
                }
            }
        }
        throw new IllegalStateException(
            "timed out waiting for spot state route " + request.spotRid(),
            lastFailure);
    }

    private Contracts.StateRes requestStageWithRetry(Contracts.SpotStageProbeRouteReq request) {
        return requestToSpotWithRetry(
            request.spotRid(),
            new Contracts.StageProbeReq(request.marker(), request.delta()),
            "StageProbeReq",
            Contracts.StateRes.class,
            "spot stage route");
    }

    private Contracts.StageTimerStartRes requestStageTimerWithRetry(Contracts.SpotStageTimerRouteReq request) {
        return requestToSpotWithRetry(
            request.spotRid(),
            new Contracts.StageTimerStartReq(request.name(), request.periodMilliseconds()),
            "StageTimerStartReq",
            Contracts.StageTimerStartRes.class,
            "spot stage timer route");
    }

    private <T> T requestToSpotWithRetry(
        String spotRid,
        Object packet,
        String packetName,
        Class<T> responseType,
        String operation) {
        long deadline = System.nanoTime() + java.time.Duration.ofSeconds(10).toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                return routes.requestToSpot(
                        Contracts.ROUTE_CHANNEL,
                        spotHandle(spotRid),
                        packet)
                    .timeout(java.time.Duration.ofSeconds(2))
                    .submit(responseType).toCompletableFuture().join();
            } catch (RuntimeException error) {
                lastFailure = error;
                try {
                    Thread.sleep(100);
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("interrupted while waiting for " + operation, interrupted);
                }
            }
        }
        throw new IllegalStateException(
            "timed out waiting for " + operation + " " + spotRid,
            lastFailure);
    }

    private static boolean fails(Runnable action) {
        try {
            action.run();
            return false;
        } catch (RuntimeException error) {
            return true;
        }
    }

    private SpotHandle spotHandle(String spotRid) {
        return spotHandles.resolveSpotHandle(RoutingId.from(spotRid)).toCompletableFuture().join()
            .orElseThrow(() -> new IllegalStateException("spot not found: " + spotRid));
    }

    private static String queryValue(URI uri, String name) {
        String query = uri.getRawQuery();
        if (query == null || query.isBlank()) {
            return null;
        }
        for (String part : query.split("&")) {
            String[] pair = part.split("=", 2);
            if (pair.length == 2 && name.equals(pair[0])) {
                return java.net.URLDecoder.decode(pair[1], StandardCharsets.UTF_8);
            }
        }
        return null;
    }

    private static void write(
        com.sun.net.httpserver.HttpExchange exchange,
        int status,
        String value) throws java.io.IOException {
        byte[] body = value.getBytes(StandardCharsets.UTF_8);
        exchange.sendResponseHeaders(status, body.length);
        exchange.getResponseBody().write(body);
        exchange.close();
    }

    private static String failureText(Throwable error) {
        StringBuilder text = new StringBuilder();
        Throwable current = error;
        while (current != null) {
            if (!text.isEmpty()) {
                text.append(" caused by ");
            }
            text.append(current.getClass().getSimpleName()).append(": ")
                .append(current.getMessage());
            current = current.getCause();
        }
        return text.append('\n').toString();
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
