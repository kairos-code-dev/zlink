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
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

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
            options.addClientServerChannel(Contracts.CHANNEL)
                .enableClient(Env.get("ZLINK_JAVA_E2E_API_ENDPOINT"));
        };
    }

    @Bean
    TriggerScenario triggerScenario(ZLinkClient client, ObjectMapper json) {
        return new TriggerScenario(client, json);
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
                        write(exchange, 200, scenario.run(name));
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
        private final HttpClient http = HttpClient.newHttpClient();

        public TriggerScenario(ZLinkClient client, ObjectMapper json) {
            this.client = client;
            this.json = json;
        }

        public String run(String name) {
            return switch (name) {
                case "mon-a1" -> monA1();
                case "mon-a2" -> monA2();
                case "mon-a3" -> monA3();
                case "mon-a5" -> monA5();
                case "mon-b1" -> monB1();
                case "mon-b2" -> monB2();
                case "mon-c1" -> monC1();
                case "mon-a4" -> unsupported("MON-A4", "failover/drain transition runner is not implemented");
                case "mon-d1" -> unsupported("MON-D1", "failure/recovery continuity runner is not implemented");
                default -> throw new IllegalArgumentException("unknown RuntimeMonitoring scenario: " + name);
            };
        }

        private String monA1() {
            request("a1-0");
            waitForAnyEvent(Env.get("ZLINK_JAVA_E2E_SERVICE_HTTP"), "socket", Set.of(
                "CONNECTED",
                "CONNECTION_READY"));
            return "scenario MON-A1 passed\n";
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

        private String monC1() {
            waitForEvent(Env.get("ZLINK_JAVA_E2E_SERVICE_HTTP"), "monitoring", Set.of(
                "HandlerFailureInjected"));
            Contracts.WorkRes reply = request("c1-after-handler-failure");
            ensure(reply.value().equals("work:c1-after-handler-failure"),
                "MON-C1 follow-up reply mismatch");
            return "scenario MON-C1 passed\n";
        }

        private String unsupported(String scenario, String reason) {
            throw new IllegalStateException(scenario + " is a documented Java gap: " + reason);
        }

        private Contracts.WorkRes request(String value) {
            Contracts.WorkRes reply = client.requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.WorkReq(value))
                .timeout(Duration.ofSeconds(3))
                .await(Contracts.WorkRes.class);
            ensure(reply.value().equals("work:" + value), "reply mismatch for " + value);
            return reply;
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

}
