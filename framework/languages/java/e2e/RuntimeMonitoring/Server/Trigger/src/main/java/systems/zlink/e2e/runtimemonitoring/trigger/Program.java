package systems.zlink.e2e.runtimemonitoring.trigger;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.concurrent.CompletionException;
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
        Env.configure(args);
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        builder.run();
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    ZLinkFrameworkConfigurer triggerFramework() {
        return options -> {
            String logDir = Env.get("logDirectory", "logs");
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/trigger-flow.log")
                .traceLabel("java-mon-trigger");
            var channel = options.addClientServerChannel(Contracts.CHANNEL)
                .enableClient(Env.get("apiEndpoint"));
            String serviceBEndpoint = Env.get("serviceBApiEndpoint");
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
    TriggerEndpoints triggerEndpoints(ZLinkClient client, ObjectMapper json, TriggerEvidence evidence) {
        return new TriggerEndpoints(
            client,
            json,
            evidence,
            Env.get("triggerHttpEndpoint"));
    }

    public static final class TriggerEndpoints implements SmartLifecycle {
        private final ZLinkClient client;
        private final ObjectMapper json;
        private final TriggerEvidence evidence;
        private final String endpoint;
        private com.sun.net.httpserver.HttpServer server;
        private boolean running;

        public TriggerEndpoints(
            ZLinkClient client,
            ObjectMapper json,
            TriggerEvidence evidence,
            String endpoint) {
            this.client = client;
            this.json = json;
            this.evidence = evidence;
            this.endpoint = endpoint;
        }

        @Override
        public void start() {
            try {
                URI uri = URI.create(endpoint);
                server = com.sun.net.httpserver.HttpServer.create(
                    new InetSocketAddress(uri.getHost(), uri.getPort()), 0);
                server.createContext("/health", exchange -> write(exchange, "ok\n"));
                server.createContext("/evidence", exchange ->
                    writeJson(exchange, 200, evidence.snapshot()));
                server.createContext("/request", exchange -> {
                    try {
                        Contracts.WorkReq request = json.readValue(
                            exchange.getRequestBody(), Contracts.WorkReq.class);
                        Contracts.WorkRes result = client.requestToChannel(Contracts.CHANNEL, request)
                            .timeout(Duration.ofSeconds(3))
                            .submit(Contracts.WorkRes.class)
                            .toCompletableFuture()
                            .join();
                        writeJson(exchange, 200, result);
                    } catch (Throwable error) {
                        Throwable cause = error instanceof CompletionException
                            && error.getCause() != null ? error.getCause() : error;
                        writeJson(exchange, 500, new OperationFailure(cause.toString()));
                    }
                });
                server.createContext("/validation/", exchange -> {
                    String name = exchange.getRequestURI().getPath()
                        .substring("/validation/".length());
                    writeJson(exchange, 200, validation(name));
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

        private void writeJson(
            com.sun.net.httpserver.HttpExchange exchange,
            int status,
            Object value) throws IOException {
            byte[] body = json.writeValueAsBytes(value);
            exchange.getResponseHeaders().add("Content-Type", "application/json");
            exchange.sendResponseHeaders(status, body.length);
            exchange.getResponseBody().write(body);
            exchange.close();
        }

        private static ValidationResult validation(String name) {
            Class<?> configClass = switch (name) {
                case "bad-interval" -> BadIntervalConfig.class;
                case "missing-socket" -> MissingSocketSourceConfig.class;
                case "missing-spot" -> MissingSpotSourceConfig.class;
                default -> throw new IllegalArgumentException(
                    "unknown monitoring validation case: " + name);
            };
            try (var context = new SpringApplicationBuilder(configClass)
                     .web(WebApplicationType.NONE)
                     .run()) {
                return new ValidationResult(false, "configuration unexpectedly started");
            } catch (Throwable error) {
                StringBuilder messages = new StringBuilder();
                Throwable current = error;
                while (current != null) {
                    if (current.getMessage() != null) {
                        if (!messages.isEmpty()) {
                            messages.append(" | ");
                        }
                        messages.append(current.getMessage());
                    }
                    current = current.getCause();
                }
                return new ValidationResult(true, messages.toString());
            }
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

        private record OperationFailure(String error) {
        }

        private record ValidationResult(boolean rejected, String message) {
        }
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

        public synchronized Contracts.EvidenceSnapshot snapshot() {
            return new Contracts.EvidenceSnapshot(java.util.List.copyOf(entries));
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
