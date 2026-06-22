package systems.zlink.framework.scenarioe2e;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.time.Instant;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.TimeoutException;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.SpringBootConfiguration;
import org.springframework.boot.autoconfigure.EnableAutoConfiguration;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class ScenarioE2E {
    private static final ObjectMapper MAPPER = new ObjectMapper()
        .findAndRegisterModules()
        .enable(SerializationFeature.INDENT_OUTPUT);
    private static ScenarioOptions currentOptions;

    private ScenarioE2E() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 0) {
            throw new IllegalArgumentException("scenario E2E role is required.");
        }
        String role = args[0];
        ScenarioOptions options = ScenarioOptions.parse(slice(args));
        currentOptions = options;
        if ("server".equals(role)) {
            runServer(options);
            return;
        }
        if ("client".equals(role)) {
            runClient(options);
            return;
        }
        throw new IllegalArgumentException("Unknown scenario E2E role '" + role + "'.");
    }

    private static void runServer(ScenarioOptions options) throws Exception {
        Files.createDirectories(Path.of(options.workDir()));
        var builder = new SpringApplicationBuilder(ScenarioServerApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        try (ConfigurableApplicationContext context = builder.run()) {
            ScenarioEvidence evidence = context.getBean(ScenarioEvidence.class);
            HttpServer server = HttpServer.create(new InetSocketAddress("127.0.0.1", URI.create(options.httpEndpoint()).getPort()), 0);
            server.createContext("/health", exchange -> writeJson(exchange, 200, Map.of("status", "ready")));
	            server.createContext("/evidence", exchange -> writeJson(exchange, 200, new ScenarioEvidenceSnapshot(
	                evidence.serverId(),
	                evidence.snapshotRequestIds(),
	                evidence.snapshotCommandIds(),
	                evidence.snapshotCompletedRequestIds(),
	                evidence.snapshotRouteRequestIds(),
	                evidence.snapshotRouteSourceRids())));
            server.start();
            Files.writeString(Path.of(options.readyFile()), "ready");
            waitForShutdown();
            server.stop(0);
        }
    }

    private static void runClient(ScenarioOptions options) throws Exception {
        Files.createDirectories(Path.of(options.workDir()));
        ScenarioDocument scenario = MAPPER.readValue(Path.of(options.scenarioFile()).toFile(), ScenarioDocument.class);

	        ensure("CH-001".equals(scenario.id()) || "CH-002".equals(scenario.id()) || "CH-004".equals(scenario.id())
	                || "CH-006".equals(scenario.id()) || "CH-007".equals(scenario.id()),
	            "scenario id must be CH-001, CH-002, CH-004, CH-006, or CH-007");
        ensure(scenario.roles().server().stream().anyMatch(server -> server.channel().equals(options.channel())),
            "scenario server channel must match the configured channel");
        ensure(!scenario.roles().client().isEmpty(), "scenario must declare at least one client role");
        ensure(Path.of(options.workDir(), scenario.artifacts().logs()).toAbsolutePath().normalize()
                .equals(Path.of(options.logDir()).toAbsolutePath().normalize()),
            "scenario logs artifact must match configured log directory");
        ensure(Path.of(options.workDir(), scenario.artifacts().report()).toAbsolutePath().normalize()
                .equals(Path.of(options.reportFile()).toAbsolutePath().normalize()),
            "scenario report artifact must match configured report file");
        ensure("server:/evidence".equals(scenario.artifacts().evidence())
                || "servers:/evidence".equals(scenario.artifacts().evidence()),
            "scenario evidence artifact must use supported evidence endpoint");

        var builder = new SpringApplicationBuilder(ScenarioClientApplication.class)
            .web(WebApplicationType.NONE);
	        try (ConfigurableApplicationContext context = builder.run()) {
	            ZLinkClient client = context.getBean(ZLinkClient.class);
	            ZLinkRouteClient routeClient = context.getBean(ZLinkRouteClient.class);
	            List<ZLinkHttpClient> httpClients = options.httpEndpoints().stream()
                .map(endpoint -> ZLinkHttpClient.create(endpoint).json().timeout(Duration.ofSeconds(3)).build())
                .toList();
            ZLinkHttpClient http = httpClients.getFirst();
            Map<String, Integer> repliesByServer = new HashMap<>();

            try {
                for (ScenarioStep step : scenario.steps()) {
                    if (step.waitReady() != null) {
                        String[] targets = step.waitReady().targets() != null
                            ? step.waitReady().targets()
                            : new String[] { step.waitReady().target() };
                        List<ZLinkHttpClient> selectedHttpClients = targets.length == 1
                            ? List.of(http)
                            : httpClients;
                        ensure(selectedHttpClients.size() == targets.length,
                            "readiness endpoint count must match server targets");
                        boolean serverReady = false;
                        Instant deadline = Instant.now().plusSeconds(10);
                        while (Instant.now().isBefore(deadline) && !serverReady) {
                            serverReady = true;
                            for (ZLinkHttpClient endpoint : selectedHttpClients) {
                                try {
                                    serverReady = serverReady
                                        && endpoint.get("/health").submitRaw().toCompletableFuture().join().status() == 200;
                                } catch (RuntimeException ignored) {
                                    serverReady = false;
                                }
                            }
                            if (!serverReady) {
                                Thread.sleep(100);
                            }
                        }

                        ensure(Arrays.stream(targets).allMatch(target -> target.startsWith("server:")),
                            "readiness targets must be server roles");
                        ensure(serverReady, "server readiness must be reached before client request");
                        continue;
                    }

                    if (step.clientRequest() != null) {
                        ScenarioClientRequestStep request = step.clientRequest();
                        ensure(request.client().equals(scenario.roles().client().getFirst().name()),
                            "request client must match scenario role");
                        ensure(request.channel().equals(options.channel()), "request channel must match configured channel");
                        ensure("ScenarioProfileRequest".equals(request.packetName()), "request packet name must match handler");

                        ScenarioProfileReply reply = client
                            .requestToChannel(
                                request.channel(),
                                new ScenarioProfileRequest(request.requestId(), request.value()))
                            .packetName(request.packetName())
                            .timeout(Duration.ofSeconds(3))
                            .await(ScenarioProfileReply.class);

                        ensure(reply.requestId().equals(request.requestId()), "reply request id must match");
                        ensure(reply.value().equals(request.expectReply()), "reply value must match scenario expectation");
                        if (scenario.expect().reply().requestId() != null
                            && scenario.expect().reply().requestId().equals(request.requestId())) {
                            ensure(reply.requestId().equals(scenario.expect().reply().requestId()),
                                "reply request id must match expect block");
                            ensure(reply.value().equals(scenario.expect().reply().value()),
                                "reply value must match expect block");
                        }
                        continue;
                    }

                    if (step.clientTimeoutRequest() != null) {
                        ScenarioClientTimeoutRequestStep request = step.clientTimeoutRequest();
                        ensure(request.client().equals(scenario.roles().client().getFirst().name()),
                            "timeout request client must match scenario role");
                        ensure(request.channel().equals(options.channel()),
                            "timeout request channel must match configured channel");
                        ensure("ScenarioProfileRequest".equals(request.packetName()),
                            "timeout request packet name must match handler");

                        CompletableFuture<ScenarioProfileReply> timeoutRequest = client
                            .requestToChannel(
                                request.channel(),
                                new ScenarioProfileRequest(request.requestId(), request.value()))
                            .packetName(request.packetName())
                            .timeout(Duration.ofMillis(request.timeoutMs()))
                            .submit(ScenarioProfileReply.class)
                            .toCompletableFuture();

                        try {
                            timeoutRequest.join();
                            ensure(false, "timeout request must fail with public timeout error");
                        } catch (CompletionException ex) {
                            ensure(ex.getCause() instanceof TimeoutException,
                                "timeout request must fail with java.util.concurrent.TimeoutException");
                        }
                        continue;
                    }

                    if (step.concurrentTimeoutAndRequest() != null) {
                        ScenarioConcurrentTimeoutAndRequestStep request = step.concurrentTimeoutAndRequest();
                        ensure(request.client().equals(scenario.roles().client().getFirst().name()),
                            "concurrent request client must match scenario role");
                        ensure(request.channel().equals(options.channel()),
                            "concurrent request channel must match configured channel");
                        ensure("ScenarioProfileRequest".equals(request.packetName()),
                            "concurrent request packet name must match handler");

                        CompletableFuture<ScenarioProfileReply> slowRequest = client
                            .requestToChannel(
                                request.channel(),
                                new ScenarioProfileRequest(request.timeoutRequestId(), request.timeoutValue()))
                            .packetName(request.packetName())
                            .timeout(Duration.ofMillis(request.timeoutMs()))
                            .submit(ScenarioProfileReply.class)
                            .toCompletableFuture();

                        ScenarioProfileReply reply = client
                            .requestToChannel(
                                request.channel(),
                                new ScenarioProfileRequest(request.normalRequestId(), request.normalValue()))
                            .packetName(request.packetName())
                            .timeout(Duration.ofSeconds(3))
                            .await(ScenarioProfileReply.class);
                        ensure(reply.requestId().equals(request.normalRequestId()),
                            "concurrent normal reply request id must match");
                        ensure(reply.value().equals(request.expectNormalReply()),
                            "concurrent normal reply value must match");

                        try {
                            slowRequest.join();
                            ensure(false, "concurrent slow request must fail with public timeout error");
                        } catch (CompletionException ex) {
                            ensure(ex.getCause() instanceof TimeoutException,
                                "concurrent slow request must fail with java.util.concurrent.TimeoutException");
                        }
                        continue;
                    }

                    if (step.waitFor() != null) {
                        ScenarioWaitForStep waitFor = step.waitFor();
                        ensure(waitFor.durationMs() > 0, "waitFor duration must be positive");
                        Thread.sleep(waitFor.durationMs());
                        continue;
                    }

                    if (step.roundRobinRequest() != null) {
                        ScenarioRoundRobinRequestStep request = step.roundRobinRequest();
                        ensure(request.client().equals(scenario.roles().client().getFirst().name()),
                            "request client must match scenario role");
                        ensure(request.channel().equals(options.channel()), "request channel must match configured channel");
                        ensure("ScenarioProfileRequest".equals(request.packetName()), "request packet name must match handler");
                        ensure(request.requestCount() == 90, "CH-002 request count must be 90");
                        ensure(request.warmupCount() == 3, "CH-002 warm-up count must be 3");

                        for (int index = 0; index < request.warmupCount(); index++) {
                            String requestId = request.requestIdPrefix() + "-warmup-" + index;
                            String value = request.valuePrefix() + "-warmup-" + index;
                            ScenarioProfileReply reply = client
                                .requestToChannel(request.channel(), new ScenarioProfileRequest(requestId, value))
                                .packetName(request.packetName())
                                .timeout(Duration.ofSeconds(3))
                                .await(ScenarioProfileReply.class);
                            ensure(reply.requestId().equals(requestId), "warm-up reply request id must match");
                        }

                        for (int index = 0; index < request.requestCount(); index++) {
                            String requestId = request.requestIdPrefix() + "-" + index;
                            String value = request.valuePrefix() + "-" + index;
                            ScenarioProfileReply reply = client
                                .requestToChannel(request.channel(), new ScenarioProfileRequest(requestId, value))
                                .packetName(request.packetName())
                                .timeout(Duration.ofSeconds(3))
                                .await(ScenarioProfileReply.class);
                            ensure(reply.requestId().equals(requestId), "round-robin reply request id must match");
                            ensure(reply.value().equals("profile:" + value), "round-robin reply value must match");
                            ensure(reply.serverId() != null && !reply.serverId().isBlank(),
                                "round-robin reply must include server id");
                            repliesByServer.merge(reply.serverId(), 1, Integer::sum);
                        }
	                        continue;
	                    }

	                    if (step.routeTargetedRequest() != null) {
	                        ScenarioRouteTargetedRequestStep request = step.routeTargetedRequest();
	                        ensure(request.client().equals(scenario.roles().client().getFirst().name()),
	                            "route request client must match scenario role");
	                        ensure(request.channel().equals(options.channel()), "route request channel must match configured channel");
	                        ensure("ScenarioRoutePing".equals(request.packetName()), "route request packet name must match handler");

	                        ScenarioRoutePingReply reply = routeClient
	                            .requestTo(
	                                request.channel(),
	                                RoutingId.from(request.targetRid()),
	                                new ScenarioRoutePingRequest(request.requestId(), request.value()))
	                            .packetName(request.packetName())
	                            .timeout(Duration.ofSeconds(3))
	                            .await(ScenarioRoutePingReply.class);
	                        ensure(reply.requestId().equals(request.requestId()), "route reply request id must match");
	                        ensure(reply.value().equals(request.expectReply()), "route reply value must match");
	                        ensure(reply.targetRid().equals(request.targetRid()), "route reply target routing id must match");
	                        ensure(reply.sourceRid().equals(request.sourceRid()), "route reply source routing id must match");

	                        try {
	                            routeClient
	                                .requestTo(
	                                    request.channel(),
	                                    RoutingId.from(request.missingRid()),
	                                    new ScenarioRoutePingRequest(request.requestId() + "-missing", request.value()))
	                                .packetName(request.packetName())
	                                .timeout(Duration.ofMillis(request.missingTimeoutMs()))
	                                .await(ScenarioRoutePingReply.class);
	                            ensure(false, "missing route request must fail with public route error");
	                        } catch (RuntimeException ex) {
	                            ensure(isPublicRouteError(ex), "missing route request must expose public route error: " + ex);
	                        }

	                        Map<String, ZLinkHttpClient> httpByServer = new HashMap<>();
	                        for (int index = 0; index < scenario.roles().server().size(); index++) {
	                            httpByServer.put(scenario.roles().server().get(index).name(), httpClients.get(index));
	                        }
	                        ZLinkHttpClient targetHttp = httpByServer.get(request.targetRid());
	                        ZLinkHttpClient otherHttp = httpByServer.get(request.nonTargetRid());
	                        ensure(targetHttp != null, "target route peer evidence endpoint must exist");
	                        ensure(otherHttp != null, "non-target route peer evidence endpoint must exist");

	                        ScenarioEvidenceSnapshot targetEvidence = new ScenarioEvidenceSnapshot(
	                            "", new String[0], new String[0], new String[0], new String[0], new String[0]);
	                        boolean targetMatched = false;
	                        Instant deadline = Instant.now().plusSeconds(10);
	                        while (Instant.now().isBefore(deadline) && !targetMatched) {
	                            targetEvidence = targetHttp.get("/evidence").fetch(ScenarioEvidenceSnapshot.class);
	                            targetMatched = List.of(targetEvidence.routeRequestIds()).contains(request.requestId())
	                                && List.of(targetEvidence.routeSourceRids()).contains(request.sourceRid());
	                            if (!targetMatched) {
	                                Thread.sleep(100);
	                            }
	                        }
	                        ensure(targetMatched, "target route peer evidence must include request and source routing id");
	                        ensure(List.of(targetEvidence.routeRequestIds()).containsAll(
	                                listOrEmpty(scenario.expect().serverEvidence().routeRequestIds())),
	                            "target route peer evidence must include expected route request ids");
	                        ensure(List.of(targetEvidence.routeSourceRids()).containsAll(
	                                listOrEmpty(scenario.expect().serverEvidence().routeSourceRids())),
	                            "target route peer evidence must include expected source routing ids");

	                        Instant nonTargetDeadline = Instant.now().plusMillis(1000);
	                        while (Instant.now().isBefore(nonTargetDeadline)) {
	                            ScenarioEvidenceSnapshot otherEvidence =
	                                otherHttp.get("/evidence").fetch(ScenarioEvidenceSnapshot.class);
	                            ensure(!List.of(otherEvidence.routeRequestIds()).contains(request.requestId()),
	                                "non-target route peer evidence must not include targeted request");
	                            ensure(listOrEmpty(scenario.expect().serverEvidence().routeRequestIds()).stream()
	                                    .noneMatch(expected -> List.of(otherEvidence.routeRequestIds()).contains(expected)),
	                                "non-target route peer evidence must not include expected route request ids");
	                            Thread.sleep(100);
	                        }
	                        continue;
	                    }

	                    if (step.assertEvidence() != null) {
                        ScenarioAssertEvidenceStep evidenceStep = step.assertEvidence();
                        ensure("server:api-a".equals(evidenceStep.target()), "evidence target must be server:api-a");
                        ScenarioEvidenceSnapshot evidence = new ScenarioEvidenceSnapshot(
	                            "",
	                            new String[0],
	                            new String[0],
	                            new String[0],
	                            new String[0],
	                            new String[0]);
                        Instant deadline = Instant.now().plusSeconds(10);
                        boolean evidenceMatched = false;
                        while (Instant.now().isBefore(deadline) && !evidenceMatched) {
                            evidence = http.get("/evidence").fetch(ScenarioEvidenceSnapshot.class);
                            boolean requestMatched = evidenceStep.requestId() == null
                                || evidenceStep.requestId().isBlank()
                                || List.of(evidence.requestIds()).contains(evidenceStep.requestId());
                            boolean commandMatched = evidenceStep.commandId() == null
                                || evidenceStep.commandId().isBlank()
                                || List.of(evidence.commandIds()).contains(evidenceStep.commandId());
                            boolean completedMatched = evidenceStep.completedRequestId() == null
                                || evidenceStep.completedRequestId().isBlank()
                                || List.of(evidence.completedRequestIds()).contains(evidenceStep.completedRequestId());
                            evidenceMatched = requestMatched && commandMatched && completedMatched;
                            if (!evidenceMatched) {
                                Thread.sleep(100);
                            }
                        }
                        ensure(evidenceMatched, "server evidence must match scenario step before timeout");
                        if (evidenceStep.requestId() != null && !evidenceStep.requestId().isBlank()) {
                            ensure(List.of(evidence.requestIds()).contains(evidenceStep.requestId()),
                                "server evidence must include request id from step");
                        }
                        if (evidenceStep.commandId() != null && !evidenceStep.commandId().isBlank()) {
                            ensure(List.of(evidence.commandIds()).contains(evidenceStep.commandId()),
                                "server evidence must include command id from step");
                        }
                        if (evidenceStep.completedRequestId() != null && !evidenceStep.completedRequestId().isBlank()) {
                            ensure(List.of(evidence.completedRequestIds()).contains(evidenceStep.completedRequestId()),
                                "server evidence must include completed request id from step");
                        }
                        ensure(List.of(evidence.requestIds()).containsAll(
                                listOrEmpty(scenario.expect().serverEvidence().requestIds())),
                            "server evidence must include all expected request ids");
                        ensure(List.of(evidence.commandIds()).containsAll(
                                listOrEmpty(scenario.expect().serverEvidence().commandIds())),
                            "server evidence must include all expected command ids");
                        ensure(List.of(evidence.completedRequestIds()).containsAll(
                                listOrEmpty(scenario.expect().serverEvidence().completedRequestIds())),
                            "server evidence must include all expected completed request ids");
                        continue;
                    }

                    if (step.clientSend() != null) {
                        ScenarioClientSendStep send = step.clientSend();
                        ensure(send.client().equals(scenario.roles().client().getFirst().name()),
                            "send client must match scenario role");
                        ensure(send.channel().equals(options.channel()), "send channel must match configured channel");
                        ensure("ScenarioProfileCommand".equals(send.packetName()),
                            "send packet name must match handler");
                        client
                            .sendToChannel(send.channel(), new ScenarioProfileCommand(send.commandId(), send.value()))
                            .packetName(send.packetName())
                            .await();
                        continue;
                    }

                    if (step.assertDistribution() != null) {
                        for (Map.Entry<String, Integer> expected : step.assertDistribution().expectedCounts().entrySet()) {
                            ensure(repliesByServer.getOrDefault(expected.getKey(), 0).equals(expected.getValue()),
                                "round-robin reply count for " + expected.getKey() + " must be " + expected.getValue());
                        }

                        for (ZLinkHttpClient endpoint : httpClients) {
                            ScenarioEvidenceSnapshot evidence = endpoint.get("/evidence").fetch(ScenarioEvidenceSnapshot.class);
                            Integer expectedCount = step.assertDistribution().expectedCounts().get(evidence.serverId());
                            ensure(expectedCount != null, "server evidence must identify a known server");
                            long verifiedRequests = Arrays.stream(evidence.requestIds())
                                .filter(requestId -> requestId.startsWith(scenario.expect().reply().requestIdPrefix() + "-")
                                    && !requestId.contains("-warmup-"))
                                .count();
                            ensure(verifiedRequests == expectedCount,
                                "server evidence count for " + evidence.serverId() + " must be " + expectedCount);
                        }
                        continue;
                    }

                    throw new IllegalStateException("scenario step must contain a known action.");
                }
            } finally {
                for (ZLinkHttpClient endpoint : httpClients) {
                    endpoint.close();
                }
            }

            ScenarioReport report = new ScenarioReport(
                scenario.id(),
                "java",
                System.getProperty("java.version"),
                "tcp",
                "json",
                "passed",
                options.scenarioFile(),
	                scenario.roles().server().size() + scenario.roles().client().size(),
                1,
                0,
                0,
                options.logDir(),
                String.join(",", options.httpEndpoints().stream()
                    .map(endpoint -> endpoint + "/evidence")
                    .toList()),
                options.scriptPath(),
	                options.processIds(scenario.roles().client()),
                null,
                Instant.now().toString());
            MAPPER.writeValue(Path.of(options.reportFile()).toFile(), report);
            System.out.println("scenario=" + scenario.id() + " language=java result=passed evidence=" + options.reportFile());
        }
    }

    private static String[] slice(String[] args) {
        String[] result = new String[args.length - 1];
        System.arraycopy(args, 1, result, 0, result.length);
        return result;
    }

    private static void writeJson(HttpExchange exchange, int status, Object body) {
        try (exchange) {
            byte[] bytes = MAPPER.writeValueAsBytes(body);
            exchange.getResponseHeaders().set("content-type", "application/json");
            exchange.sendResponseHeaders(status, bytes.length);
            exchange.getResponseBody().write(bytes);
        } catch (IOException cause) {
            throw new UncheckedIOException(cause);
        }
    }

    private static void waitForShutdown() throws InterruptedException {
        Object monitor = new Object();
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            synchronized (monitor) {
                monitor.notifyAll();
            }
        }));
        synchronized (monitor) {
            monitor.wait();
        }
    }

    private static void ensure(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException("ensure failed: " + message);
        }
    }

    private static List<String> listOrEmpty(List<String> values) {
        return values == null ? List.of() : values;
    }

    private static boolean isPublicRouteError(Throwable error) {
        Throwable current = error;
        while (current != null) {
            if (current instanceof TimeoutException) {
                return true;
            }
            String message = current.getMessage();
            if (message != null
                && (message.toLowerCase(java.util.Locale.ROOT).contains("timeout")
	                    || message.toLowerCase(java.util.Locale.ROOT).contains("not connected")
	                    || message.toLowerCase(java.util.Locale.ROOT).contains("not_connected")
                    || message.toLowerCase(java.util.Locale.ROOT).contains("not ready")
                    || message.toLowerCase(java.util.Locale.ROOT).contains("host unreachable")
                    || message.toLowerCase(java.util.Locale.ROOT).contains("routenotconnected"))) {
                return true;
            }
            current = current.getCause();
        }
        return false;
    }

    @EnableZLinkFramework
    @EnableAutoConfiguration
    @SpringBootConfiguration(proxyBeanMethods = false)
    static class ScenarioServerApplication {
        @Bean
        ScenarioEvidence scenarioEvidence() {
            return new ScenarioEvidence();
        }

        @Bean
        ZLinkFrameworkConfigurer scenarioServerFramework() {
            return options -> {
                options.codecs().addJson();
                if ("route-peer".equals(currentOptions.mode())) {
                    var routed = options.addRouteMeshChannel(currentOptions.channel())
                        .enableServer(currentOptions.zlinkEndpoint());
                    routed.setRoutingId(RoutingId.from(currentOptions.serverName()));
                    for (String endpoint : currentOptions.routePeerEndpoints()) {
                        routed.enableClient(endpoint);
                    }
                    routed.addRequestHandler(
                        ScenarioRoutePingHandler.class,
                        ScenarioRoutePingRequest.class,
                        ScenarioRoutePingReply.class,
                        "ScenarioRoutePing");
                    return;
                }

                var channel = options.addClientServerChannel(currentOptions.channel())
                    .enableServer(currentOptions.zlinkEndpoint());
                channel.addRequestHandler(
                    ScenarioProfileHandler.class,
                    ScenarioProfileRequest.class,
                    ScenarioProfileReply.class,
                    "ScenarioProfileRequest");
                channel.addSendHandler(
                    ScenarioProfileCommandHandler.class,
                    ScenarioProfileCommand.class,
                    "ScenarioProfileCommand");
            };
        }
    }

    @EnableZLinkFramework
    @EnableAutoConfiguration
    @SpringBootConfiguration(proxyBeanMethods = false)
    static class ScenarioClientApplication {
        @Bean
        ZLinkFrameworkConfigurer scenarioClientFramework() {
            return options -> {
                options.codecs().addJson();
                if ("route-peer".equals(currentOptions.mode())) {
                    var routed = options.addRouteMeshChannel(currentOptions.channel())
                        .enableServer(currentOptions.zlinkEndpoint());
                    routed.setRoutingId(RoutingId.from(currentOptions.clientRoutingId()));
                    for (String endpoint : currentOptions.routePeerEndpoints()) {
                        routed.enableClient(endpoint);
                    }
                    return;
                }

                var channel = options.addClientServerChannel(currentOptions.channel());
                for (String endpoint : currentOptions.zlinkEndpoints()) {
                    channel.enableClient(endpoint);
                }
            };
        }
    }

    public static final class ScenarioProfileHandler
        implements ZLinkRequestHandler<ScenarioProfileRequest, ScenarioProfileReply> {
        private final ScenarioEvidence evidence;

        public ScenarioProfileHandler(ScenarioEvidence evidence) {
            this.evidence = evidence;
        }

        @Override
        public ScenarioProfileReply handle(ScenarioProfileRequest request, ZLinkRequestContext context) {
            evidence.requestIds().add(request.requestId());
            if ("slow".equals(request.value()) || request.requestId().contains("timeout")) {
                try {
                    Thread.sleep(1000);
                } catch (InterruptedException ex) {
                    Thread.currentThread().interrupt();
                }
            }
            evidence.completedRequestIds().add(request.requestId());
            return new ScenarioProfileReply(request.requestId(), "profile:" + request.value(), evidence.serverId());
        }
    }

    public record ScenarioProfileRequest(String requestId, String value) {
    }

    public record ScenarioProfileReply(String requestId, String value, String serverId) {
    }

	    public record ScenarioProfileCommand(String commandId, String value) {
	    }

	    public record ScenarioRoutePingRequest(String requestId, String value) {
	    }

	    public record ScenarioRoutePingReply(String requestId, String value, String targetRid, String sourceRid) {
	    }

	    public static final class ScenarioRoutePingHandler
	        implements ZLinkRouteRequestHandler<ScenarioRoutePingRequest, ScenarioRoutePingReply> {
	        private final ScenarioEvidence evidence;

	        public ScenarioRoutePingHandler(ScenarioEvidence evidence) {
	            this.evidence = evidence;
	        }

	        @Override
	        public ScenarioRoutePingReply handle(ScenarioRoutePingRequest request, ZLinkRouteRequestContext context) {
	            evidence.routeRequestIds().add(request.requestId());
	            evidence.routeSourceRids().add(context.routingId().toString());
	            return new ScenarioRoutePingReply(
	                request.requestId(),
	                "route:" + request.value(),
	                evidence.serverId(),
	                context.routingId().toString());
	        }
	    }

    public static final class ScenarioProfileCommandHandler
        implements ZLinkSendHandler<ScenarioProfileCommand> {
        private final ScenarioEvidence evidence;

        public ScenarioProfileCommandHandler(ScenarioEvidence evidence) {
            this.evidence = evidence;
        }

        @Override
        public void handle(ScenarioProfileCommand message, ZLinkSendContext context) {
            evidence.commandIds().add(message.commandId());
        }
    }

    public record ScenarioEvidence(
        String serverId,
        ConcurrentLinkedQueue<String> requestIds,
        ConcurrentLinkedQueue<String> commandIds,
        ConcurrentLinkedQueue<String> completedRequestIds,
        ConcurrentLinkedQueue<String> routeRequestIds,
        ConcurrentLinkedQueue<String> routeSourceRids) {
        ScenarioEvidence() {
            this(
                currentOptions.serverName(),
                new ConcurrentLinkedQueue<>(),
                new ConcurrentLinkedQueue<>(),
                new ConcurrentLinkedQueue<>(),
                new ConcurrentLinkedQueue<>(),
                new ConcurrentLinkedQueue<>());
        }

        String[] snapshotRequestIds() {
            return requestIds.toArray(String[]::new);
        }

        String[] snapshotCommandIds() {
            return commandIds.toArray(String[]::new);
        }

        String[] snapshotCompletedRequestIds() {
            return completedRequestIds.toArray(String[]::new);
        }

        String[] snapshotRouteRequestIds() {
            return routeRequestIds.toArray(String[]::new);
        }

        String[] snapshotRouteSourceRids() {
            return routeSourceRids.toArray(String[]::new);
        }
    }

    public record ScenarioEvidenceSnapshot(
        String serverId,
        String[] requestIds,
        String[] commandIds,
        String[] completedRequestIds,
        String[] routeRequestIds,
        String[] routeSourceRids) {
    }

    public record ScenarioDocument(
        String id,
        String name,
        ScenarioRoles roles,
        List<ScenarioStep> steps,
        ScenarioExpect expect,
        ScenarioArtifacts artifacts) {
    }

    public record ScenarioRoles(
        ScenarioRegistryRole registry,
        List<ScenarioServerRole> server,
        List<ScenarioClientRole> client) {
    }

    public record ScenarioRegistryRole(int count) {
    }

    public record ScenarioServerRole(String name, String channel) {
    }

    public record ScenarioClientRole(String name, String routingId) {
    }

    public record ScenarioStep(
        ScenarioWaitReadyStep waitReady,
        ScenarioClientRequestStep clientRequest,
        ScenarioClientTimeoutRequestStep clientTimeoutRequest,
        ScenarioConcurrentTimeoutAndRequestStep concurrentTimeoutAndRequest,
        ScenarioWaitForStep waitFor,
        ScenarioClientSendStep clientSend,
        ScenarioRoundRobinRequestStep roundRobinRequest,
        ScenarioRouteTargetedRequestStep routeTargetedRequest,
        ScenarioAssertEvidenceStep assertEvidence,
        ScenarioAssertDistributionStep assertDistribution) {
    }

    public record ScenarioWaitReadyStep(String target, String[] targets) {
    }

    public record ScenarioClientRequestStep(
        String client,
        String channel,
        String packetName,
        String requestId,
        String value,
        String expectReply) {
    }

    public record ScenarioClientTimeoutRequestStep(
        String client,
        String channel,
        String packetName,
        String requestId,
        String value,
        int timeoutMs) {
    }

    public record ScenarioConcurrentTimeoutAndRequestStep(
        String client,
        String channel,
        String packetName,
        String timeoutRequestId,
        String timeoutValue,
        int timeoutMs,
        String normalRequestId,
        String normalValue,
        String expectNormalReply) {
    }

    public record ScenarioWaitForStep(int durationMs) {
    }

    public record ScenarioClientSendStep(
        String client,
        String channel,
        String packetName,
        String commandId,
        String value) {
    }

    public record ScenarioAssertEvidenceStep(String target, String requestId, String commandId, String completedRequestId) {
    }

	    public record ScenarioRoundRobinRequestStep(
        String client,
        String channel,
        String packetName,
        int warmupCount,
        int requestCount,
        String requestIdPrefix,
	        String valuePrefix) {
	    }

	    public record ScenarioRouteTargetedRequestStep(
	        String client,
	        String channel,
	        String packetName,
	        String targetRid,
	        String nonTargetRid,
	        String sourceRid,
	        String missingRid,
	        int missingTimeoutMs,
	        String requestId,
	        String value,
	        String expectReply) {
	    }

    public record ScenarioAssertDistributionStep(Map<String, Integer> expectedCounts) {
    }

    public record ScenarioExpect(
        ScenarioReplyExpect reply,
        ScenarioServerEvidenceExpect serverEvidence) {
    }

    public record ScenarioReplyExpect(String requestId, String value, String requestIdPrefix, String valuePrefix) {
    }

    public record ScenarioServerEvidenceExpect(
        List<String> requestIds,
        List<String> commandIds,
        List<String> completedRequestIds,
        List<String> routeRequestIds,
        List<String> routeSourceRids,
        Map<String, Integer> requestCounts) {
    }

    public record ScenarioArtifacts(String logs, String report, String evidence) {
    }

    public record ScenarioReport(
        String scenarioId,
        String language,
        String runtimeVersion,
        String transportBackend,
        String codec,
        String result,
        String scenarioFile,
        int processCount,
        int passed,
        int failed,
        int skipped,
        String logPath,
        String evidencePath,
        String executedScriptPath,
        Map<String, Long> processIds,
        String failureLayer,
        String completedAt) {
    }

    public record ScenarioOptions(
        String workDir,
        String scenarioFile,
        String zlinkEndpoint,
        String httpEndpoint,
        String channel,
        String readyFile,
        String reportFile,
	        String logDir,
	        String scriptPath,
	        String serverProcessIds,
	        String serverName,
	        String mode,
	        String routePeerEndpoint,
	        String clientRoutingId) {
        List<String> zlinkEndpoints() {
            return Arrays.stream(zlinkEndpoint.split(","))
                .map(String::trim)
                .filter(endpoint -> !endpoint.isEmpty())
                .toList();
        }

	        List<String> httpEndpoints() {
            return Arrays.stream(httpEndpoint.split(","))
                .map(String::trim)
                .filter(endpoint -> !endpoint.isEmpty())
                .toList();
	        }

	        List<String> routePeerEndpoints() {
	            return Arrays.stream(routePeerEndpoint.split(","))
	                .map(String::trim)
	                .filter(endpoint -> !endpoint.isEmpty())
	                .toList();
	        }

	        Map<String, Long> processIds(List<ScenarioClientRole> clientRoles) {
	            Map<String, Long> result = new HashMap<>();
	            result.put("client:" + clientRoles.getFirst().name(), ProcessHandle.current().pid());
	            Arrays.stream(serverProcessIds.split(","))
                .map(String::trim)
                .filter(entry -> !entry.isEmpty())
                .forEach(entry -> {
                    String[] parts = entry.split("=", 2);
                    if (parts.length != 2 || parts[0].isBlank() || parts[1].isBlank()) {
                        throw new IllegalArgumentException("server process id entry must use role=pid.");
                    }
                    result.put(parts[0], Long.parseLong(parts[1]));
                });
            return Map.copyOf(result);
        }

        static ScenarioOptions parse(String[] args) {
            java.util.HashMap<String, String> values = new java.util.HashMap<>();
            for (int index = 0; index < args.length; index += 2) {
                if (index + 1 >= args.length || !args[index].startsWith("--")) {
                    throw new IllegalArgumentException("scenario E2E arguments must use --name value pairs.");
                }
                values.put(args[index].substring(2), args[index + 1]);
            }
            String workDir = required(values, "work-dir");
            return new ScenarioOptions(
                workDir,
                values.getOrDefault("scenario", ""),
                required(values, "zlink-endpoint"),
                required(values, "http-endpoint"),
                values.getOrDefault("channel", "scenario-api"),
                values.getOrDefault("ready-file", Path.of(workDir, "server.ready").toString()),
                values.getOrDefault("report-file", Path.of(workDir, "report.json").toString()),
	                values.getOrDefault("log-dir", Path.of(workDir, "logs").toString()),
	                values.getOrDefault("script-path", ""),
	                values.getOrDefault("server-process-ids", ""),
	                values.getOrDefault("server-name", "api-a"),
	                values.getOrDefault("mode", "channel"),
	                values.getOrDefault("route-peer-endpoints", ""),
	                values.getOrDefault("client-routing-id", "client-a"));
        }

        private static String required(Map<String, String> values, String name) {
            String value = values.get(name);
            if (value == null || value.isBlank()) {
                throw new IllegalArgumentException("--" + name + " is required.");
            }
            return value;
        }
    }
}
