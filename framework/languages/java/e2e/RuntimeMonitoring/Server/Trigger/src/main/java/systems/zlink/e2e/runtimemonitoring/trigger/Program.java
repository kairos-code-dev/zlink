package systems.zlink.e2e.runtimemonitoring.trigger;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.SmartLifecycle;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;
import systems.zlink.e2e.runtimemonitoring.shared.Env;
import systems.zlink.e2e.runtimemonitoring.trigger.validation.BadIntervalConfig;
import systems.zlink.e2e.runtimemonitoring.trigger.validation.MissingSocketSourceConfig;
import systems.zlink.e2e.runtimemonitoring.trigger.validation.MissingSpotSourceConfig;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventHandler;
import systems.zlink.framework.monitoring.ZLinkSocketEvent;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.spring.ZLinkMonitoringOptionsCustomizer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.runtimemonitoring.trigger.app")
public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        builder.run(args);
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    ZLinkFrameworkConfigurer triggerFramework() {
        return options -> {
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/trigger-flow.log")
                .traceLabel("java-mon-trigger");
            var channel = options.addClientServerChannel(Contracts.CHANNEL)
                .enableClient(Env.get("ZLINK_JAVA_E2E_API_ENDPOINT"));
            String serviceBEndpoint = Env.get("ZLINK_JAVA_E2E_SERVICE_B_API_ENDPOINT");
            if (!serviceBEndpoint.isBlank()) {
                channel.enableClient(serviceBEndpoint);
            }
        };
    }

    @Bean
    ZLinkMonitoringOptionsCustomizer triggerMonitoring() {
        return options -> options.addSocketEvents(
            Contracts.CHANNEL,
            ZLinkSocketEventKind.PEER_ADMISSION_CHANGED);
    }

    @Bean
    TriggerEvidence triggerEvidence() {
        return new TriggerEvidence();
    }

    @Bean
    TriggerSocketRecorder triggerSocketRecorder(TriggerEvidence evidence) {
        return new TriggerSocketRecorder(evidence);
    }

    @Bean
    TriggerScenario triggerScenario(ZLinkClient client, ObjectMapper json, TriggerEvidence evidence) {
        return new TriggerScenario(client, json, evidence);
    }

    @Bean
    TriggerEndpoints triggerEndpoints(TriggerScenario scenario) {
        return new TriggerEndpoints(scenario, Env.get("ZLINK_JAVA_E2E_TRIGGER_HTTP"));
    }

    public static final class TriggerEndpoints implements SmartLifecycle {
        private final TriggerScenario scenario;
        private final String endpoint;
        private com.sun.net.httpserver.HttpServer server;
        private boolean running;

        public TriggerEndpoints(TriggerScenario scenario, String endpoint) {
            this.scenario = scenario;
            this.endpoint = endpoint;
        }

        @Override
        public void start() {
            try {
                URI uri = URI.create(endpoint);
                server = com.sun.net.httpserver.HttpServer.create(
                    new InetSocketAddress(uri.getHost(), uri.getPort()), 0);
                server.createContext("/health", exchange -> write(exchange, "ok\n"));
                server.createContext("/scenario/", exchange -> {
                    String name = exchange.getRequestURI().getPath().substring("/scenario/".length());
                    try {
                        scenario.run(name).whenComplete((result, failure) -> {
                            try {
                                if (failure == null) {
                                    write(exchange, 200, result);
                                } else {
                                    Throwable cause = failure instanceof CompletionException
                                        && failure.getCause() != null ? failure.getCause() : failure;
                                    write(exchange, 500, cause.getMessage() + "\n");
                                }
                            } catch (IOException writeFailure) {
                                exchange.close();
                            }
                        });
                    } catch (Throwable error) {
                        write(exchange, 500, error.getMessage() + "\n");
                    }
                });
                server.start();
                running = true;
            } catch (Exception error) {
                throw new IllegalStateException("failed to start trigger endpoint " + endpoint, error);
            }
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
            exchange.getResponseHeaders().add("Content-Type", "text/plain");
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

    public static final class TriggerScenario {
        private final ZLinkClient client;
        private final ObjectMapper json;
        private final TriggerEvidence evidence;
        private final HttpClient http = HttpClient.newHttpClient();

        public TriggerScenario(ZLinkClient client, ObjectMapper json, TriggerEvidence evidence) {
            this.client = client;
            this.json = json;
            this.evidence = evidence;
        }

        public CompletionStage<String> run(String name) {
            return switch (name) {
                case "mon-a1" -> monA1();
                case "mon-a2" -> CompletableFuture.completedFuture(monA2());
                case "mon-a3" -> CompletableFuture.completedFuture(monA3());
                case "mon-a5" -> CompletableFuture.completedFuture(monA5());
                case "mon-b1" -> CompletableFuture.completedFuture(monB1());
                case "mon-b2" -> CompletableFuture.completedFuture(monB2());
                case "mon-c1" -> monC1();
                case "mon-a4" -> monA4();
                case "mon-d1" -> monD1();
                default -> throw new IllegalArgumentException("unknown RuntimeMonitoring scenario: " + name);
            };
        }

        private CompletionStage<String> monA1() {
            return request("a1-0").thenApply(ignored -> {
                waitForAnyEvent(Env.get("ZLINK_JAVA_E2E_SERVICE_HTTP"), "socket", Set.of(
                    "CONNECTED",
                    "CONNECTION_READY"));
                return "scenario MON-A1 passed\n";
            });
        }

        private String monA2() {
            waitForEvent(Env.get("ZLINK_JAVA_E2E_SERVICE_HTTP"), "location", Set.of(
                "STATUS_CHANGED",
                "TOPOLOGY_CHANGED",
                "SERVICE_SUMMARY_CHANGED"));
            return "scenario MON-A2 passed\n";
        }

        private String monA3() {
            waitForEvent(Env.get("ZLINK_JAVA_E2E_SERVICE_HTTP"), "spot", Set.of(
                "STATUS_CHANGED",
                "PEERS_CHANGED",
                "SUBJECTS_CHANGED",
                "TIMER_HANDLER_FAILED"));
            return "scenario MON-A3 passed\n";
        }

        private String monA5() {
            triggerHandshakeFailure();
            waitForEvent(Env.get("ZLINK_JAVA_E2E_SERVICE_HTTP"), "location", Set.of("STATUS_CHANGED"));
            waitForEvent(Env.get("ZLINK_JAVA_E2E_SERVICE_HTTP"), "spot", Set.of(
                "STATUS_CHANGED",
                "TIMER_STOPPED_AFTER_UNHANDLED_EXCEPTION"));
            waitForEvent(
                Env.get("ZLINK_JAVA_E2E_SERVICE_HTTP"),
                "socket",
                Contracts.HANDSHAKE_CHANNEL,
                Set.of("DISCONNECTED"));
            return "scenario MON-A5 passed\n";
        }

        private String monB1() {
            Set<String> observed = events(
                Env.get("ZLINK_JAVA_E2E_SERVICE_HTTP"),
                "socket",
                Contracts.CHANNEL);
            ensure(observed.contains("CONNECTION_READY"),
                "MON-B1 did not observe filtered CONNECTION_READY event");
            ensure(observed.equals(Set.of("CONNECTION_READY")),
                "MON-B1 socket filter allowed unexpected events: " + observed);
            return "scenario MON-B1 passed\n";
        }

        private String monB2() {
            expectFailure(BadIntervalConfig.class, "location runtime interval must be positive");
            expectFailure(MissingSocketSourceConfig.class, "monitoring socket source is not configured");
            expectFailure(MissingSpotSourceConfig.class, "monitoring spot source is not configured");
            return "scenario MON-B2 passed\n";
        }

        private CompletionStage<String> monC1() {
            waitForEvent(Env.get("ZLINK_JAVA_E2E_SERVICE_HTTP"), "monitoring", Set.of(
                "HandlerFailureInjected"));
            return request("c1-after-handler-failure").thenApply(reply -> {
                ensure(reply.value().equals("work:c1-after-handler-failure"),
                    "MON-C1 follow-up reply mismatch");
                return "scenario MON-C1 passed\n";
            });
        }

        private CompletionStage<String> monA4() {
            String serviceA = Env.get("ZLINK_JAVA_E2E_SERVICE_HTTP");
            String serviceB = Env.get("ZLINK_JAVA_E2E_SERVICE_B_HTTP");
            if (!serviceB.isBlank()) {
                post(serviceB + "/admin/drain");
            }
            post(serviceA + "/admin/restore");
            return requestFromProvider("mon-a4-before-drain", "svc-a").thenApply(before -> {
                ensure(before.providerRid().equals("svc-a"), "MON-A4 direct trigger did not hit svc-a");
                post(serviceA + "/admin/drain");
                waitForTriggerEvent("socket", Set.of("PEER_ADMISSION_CHANGED"));
                waitForEvent(serviceA, "admin", Set.of("drain"));
                waitForEvent(serviceA, "location", Set.of("TOPOLOGY_CHANGED"));
                post(serviceA + "/admin/restore");
                if (!serviceB.isBlank()) {
                    post(serviceB + "/admin/restore");
                }
                return "scenario MON-A4 passed\n";
            });
        }

        private CompletionStage<String> monD1() {
            String serviceA = Env.get("ZLINK_JAVA_E2E_SERVICE_HTTP");
            String serviceB = Env.get("ZLINK_JAVA_E2E_SERVICE_B_HTTP");
            ensure(!serviceB.isBlank(), "MON-D1 requires ZLINK_JAVA_E2E_SERVICE_B_HTTP");
            post(serviceB + "/shutdown");
            waitForPort(serviceB, false, "MON-D1 expected service-b to stop");

            Process restarted = startServiceB();
            waitForPort(serviceB, true, "MON-D1 expected service-b to restart");
            post(serviceA + "/admin/drain");
            return requestFromProvider("mon-d1-request", "svc-b").thenApply(reply -> {
                ensure(reply.providerRid().equals("svc-b")
                        && reply.value().equals("work:mon-d1-request"),
                    "MON-D1 restarted service did not handle request");
                waitForEvent(serviceB, "work", Set.of("WorkReq"));
                waitForLocationEventCount(serviceA, 3);
                return "scenario MON-D1 passed\n";
            }).whenComplete((ignored, failure) -> {
                postBestEffort(serviceA + "/admin/restore");
                postBestEffort(serviceB + "/shutdown");
                waitForExit(restarted);
            });
        }

        private CompletionStage<Contracts.WorkRes> request(String value) {
            return client.requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.WorkReq(value))
                .timeout(Duration.ofSeconds(3))
                .submit(Contracts.WorkRes.class)
                .thenApply(reply -> {
                    ensure(reply.value().equals("work:" + value), "reply mismatch for " + value);
                    return reply;
                });
        }

        private CompletionStage<Contracts.WorkRes> requestFromProvider(String value, String providerRid) {
            return requestFromProvider(value, providerRid,
                System.nanoTime() + TimeUnit.SECONDS.toNanos(20), null);
        }

        private CompletionStage<Contracts.WorkRes> requestFromProvider(
            String value,
            String providerRid,
            long deadline,
            Contracts.WorkRes last) {
            if (System.nanoTime() >= deadline) {
                return CompletableFuture.failedFuture(new IllegalStateException(
                    "request did not reach " + providerRid + "; last=" + last));
            }
            return request(value).thenCompose(reply -> providerRid.equals(reply.providerRid())
                ? CompletableFuture.completedFuture(reply)
                : CompletableFuture.supplyAsync(
                        () -> reply,
                        CompletableFuture.delayedExecutor(200, TimeUnit.MILLISECONDS))
                    .thenCompose(ignored -> requestFromProvider(
                        value, providerRid, deadline, reply)));
        }

        private Process startServiceB() {
            ProcessBuilder builder = new ProcessBuilder(Env.get("ZLINK_JAVA_E2E_FILTERED_SERVICE_BIN"));
            builder.redirectOutput(new java.io.File(
                Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs") + "/filtered-service-restart.stdout.log"));
            builder.redirectError(new java.io.File(
                Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs") + "/filtered-service-restart.stderr.log"));
            builder.environment().put("ZLINK_JAVA_E2E_RID", "svc-b");
            builder.environment().put(
                "ZLINK_JAVA_E2E_API_ENDPOINT",
                Env.get("ZLINK_JAVA_E2E_SERVICE_B_API_ENDPOINT"));
            builder.environment().put(
                "ZLINK_JAVA_E2E_HTTP_ENDPOINT",
                Env.get("ZLINK_JAVA_E2E_SERVICE_B_HTTP"));
            builder.environment().put(
                "ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT",
                Env.get("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT"));
            builder.environment().put(
                "ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX",
                Env.get("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX"));
            builder.environment().put("ZLINK_JAVA_E2E_ENABLE_HANDSHAKE", "false");
            builder.environment().put("ZLINK_JAVA_E2E_ENABLE_SPOT", "false");
            builder.environment().put("ZLINK_JAVA_E2E_LOG_DIR", Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs"));
            try {
                return builder.start();
            } catch (IOException error) {
                throw new IllegalStateException("failed to restart service-b", error);
            }
        }

        private void waitForExit(Process process) {
            try {
                if (!process.waitFor(5, TimeUnit.SECONDS)) {
                    process.destroyForcibly();
                    process.waitFor();
                }
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("interrupted while waiting for restarted service-b", error);
            }
        }

        private void triggerHandshakeFailure() {
            String endpoint = Env.get("ZLINK_JAVA_E2E_HANDSHAKE_ENDPOINT");
            int port = Integer.parseInt(endpoint.substring(endpoint.lastIndexOf(':') + 1));
            for (int index = 0; index < 5; index++) {
                try (Socket socket = new Socket()) {
                    socket.connect(new InetSocketAddress("127.0.0.1", port), 500);
                    OutputStream output = socket.getOutputStream();
                    output.write(("invalid-zmtp-handshake-" + index).getBytes(StandardCharsets.UTF_8));
                    output.flush();
                } catch (IOException ignored) {
                    // The server is expected to reject the malformed handshake.
                }
                sleep(100);
            }
        }

        private void waitForEvent(String baseUrl, String surface, Set<String> expected) {
            waitForEvent(baseUrl, surface, "", expected);
        }

        private void waitForEvent(
            String baseUrl,
            String surface,
            String sourceName,
            Set<String> expected) {
            long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(20);
            while (System.nanoTime() < deadline) {
                Set<String> observed = events(baseUrl, surface, sourceName);
                if (observed.containsAll(expected)) {
                    return;
                }
                sleep(200);
            }
            Set<String> observed = events(baseUrl, surface, sourceName);
            throw new IllegalStateException(
                "missing " + surface + " events " + expected + " at " + baseUrl
                    + "; observed=" + observed + "; evidence=" + get(baseUrl + "/evidence"));
        }

        private void waitForAnyEvent(String baseUrl, String surface, Set<String> expected) {
            long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(20);
            while (System.nanoTime() < deadline) {
                Set<String> observed = events(baseUrl, surface);
                if (expected.stream().anyMatch(observed::contains)) {
                    return;
                }
                sleep(200);
            }
            Set<String> observed = events(baseUrl, surface);
            throw new IllegalStateException(
                "missing any " + surface + " event " + expected + " at " + baseUrl
                    + "; observed=" + observed + "; evidence=" + get(baseUrl + "/evidence"));
        }

        private void waitForTriggerEvent(String surface, Set<String> expected) {
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20);
            while (System.nanoTime() < deadline) {
                Set<String> observed = evidence.events(surface);
                if (observed.containsAll(expected)) {
                    return;
                }
                sleep(200);
            }
            throw new IllegalStateException(
                "missing trigger " + surface + " events " + expected
                    + "; observed=" + evidence.events(surface));
        }

        private void waitForLocationEventCount(String baseUrl, int expectedCount) {
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(30);
            while (System.nanoTime() < deadline) {
                int count = eventCount(baseUrl, "location", "TOPOLOGY_CHANGED");
                if (count >= expectedCount) {
                    return;
                }
                sleep(250);
            }
            throw new IllegalStateException(
                "missing location topology continuity at " + baseUrl
                    + "; evidence=" + get(baseUrl + "/evidence"));
        }

        private Set<String> events(String baseUrl, String surface) {
            return events(baseUrl, surface, "");
        }

        private Set<String> events(String baseUrl, String surface, String sourceName) {
            try {
                JsonNode root = json.readTree(get(baseUrl + "/evidence")).path("entries");
                Set<String> events = new HashSet<>();
                for (JsonNode entry : root) {
                    if (surface.equals(entry.path("surface").asText())
                        && (sourceName.isBlank()
                            || sourceName.equals(entry.path("sourceName").asText()))) {
                        events.add(entry.path("event").asText());
                    }
                }
                return events;
            } catch (Exception error) {
                return Set.of();
            }
        }

        private int eventCount(String baseUrl, String surface, String eventName) {
            try {
                JsonNode root = json.readTree(get(baseUrl + "/evidence")).path("entries");
                int count = 0;
                for (JsonNode entry : root) {
                    if (surface.equals(entry.path("surface").asText())
                        && eventName.equals(entry.path("event").asText())) {
                        count++;
                    }
                }
                return count;
            } catch (Exception error) {
                return 0;
            }
        }

        private String get(String url) {
            try {
                HttpResponse<String> response = http.send(
                    HttpRequest.newBuilder(URI.create(url))
                        .timeout(Duration.ofSeconds(3))
                        .GET()
                        .build(),
                    HttpResponse.BodyHandlers.ofString());
                ensure(response.statusCode() >= 200 && response.statusCode() < 300,
                    "GET " + url + " returned " + response.statusCode());
                return response.body();
            } catch (IOException error) {
                throw new IllegalStateException("GET failed: " + url, error);
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("GET interrupted: " + url, error);
            }
        }

        private void post(String url) {
            try {
                HttpResponse<String> response = http.send(
                    HttpRequest.newBuilder(URI.create(url))
                        .timeout(Duration.ofSeconds(3))
                        .POST(HttpRequest.BodyPublishers.noBody())
                        .build(),
                    HttpResponse.BodyHandlers.ofString());
                ensure(response.statusCode() >= 200 && response.statusCode() < 300,
                    "POST " + url + " returned " + response.statusCode() + ": " + response.body());
            } catch (IOException error) {
                throw new IllegalStateException("POST failed: " + url, error);
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("POST interrupted: " + url, error);
            }
        }

        private void postBestEffort(String url) {
            try {
                post(url);
            } catch (RuntimeException ignored) {
            }
        }

        private void waitForPort(String baseUrl, boolean open, String failureMessage) {
            URI uri = URI.create(baseUrl);
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20);
            while (System.nanoTime() < deadline) {
                if (canConnect(uri.getHost(), uri.getPort()) == open) {
                    return;
                }
                sleep(100);
            }
            throw new IllegalStateException(failureMessage);
        }

        private static boolean canConnect(String host, int port) {
            try (Socket socket = new Socket()) {
                socket.connect(new InetSocketAddress(host, port), 200);
                return true;
            } catch (IOException error) {
                return false;
            }
        }

        private static void sleep(long millis) {
            try {
                Thread.sleep(millis);
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("interrupted", error);
            }
        }

        private static void ensure(boolean condition, String message) {
            if (!condition) {
                throw new IllegalStateException(message);
            }
        }
    }

    private static void expectFailure(Class<?> configClass, String messagePart) {
        try (var context = new SpringApplicationBuilder(configClass)
                 .web(WebApplicationType.NONE)
                 .run()) {
            throw new IllegalStateException(
                configClass.getSimpleName() + " unexpectedly started");
        } catch (Throwable error) {
            if (!hasMessage(error, messagePart)) {
                throw new IllegalStateException(
                    configClass.getSimpleName() + " failed with unexpected error", error);
            }
        }
    }

    private static boolean hasMessage(Throwable error, String messagePart) {
        Throwable current = error;
        while (current != null) {
            String message = current.getMessage();
            if (message != null && message.contains(messagePart)) {
                return true;
            }
            current = current.getCause();
        }
        return false;
    }

    public static final class TriggerEvidence {
        private final java.util.List<Contracts.EvidenceEntry> entries = new java.util.ArrayList<>();

        public synchronized void record(
            String surface,
            String sourceName,
            String event,
            String detail) {
            entries.add(new Contracts.EvidenceEntry(surface, sourceName, event, detail));
        }

        public synchronized Set<String> events(String surface) {
            Set<String> events = new HashSet<>();
            for (Contracts.EvidenceEntry entry : entries) {
                if (surface.equals(entry.surface())) {
                    events.add(entry.event());
                }
            }
            return events;
        }
    }

    public static final class TriggerSocketRecorder implements ZLinkRuntimeEventHandler<ZLinkSocketEvent> {
        private final TriggerEvidence evidence;

        public TriggerSocketRecorder(TriggerEvidence evidence) {
            this.evidence = evidence;
        }

        @Override
        public void handle(ZLinkSocketEvent event) {
            evidence.record(
                "socket",
                event.sourceName(),
                event.event().name(),
                event.localAddr() + "|" + event.remoteAddr());
        }
    }

}
