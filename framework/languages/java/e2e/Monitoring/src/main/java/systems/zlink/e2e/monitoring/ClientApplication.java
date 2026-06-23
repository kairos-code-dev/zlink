package systems.zlink.e2e.monitoring;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.util.HashSet;
import java.util.Set;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.monitoring.client")
public final class ClientApplication {
    private ClientApplication() {
    }

    public static void run(String... args) {
        ConfigurableApplicationContext context =
            new SpringApplicationBuilder(ClientApplication.class)
                .web(WebApplicationType.NONE)
                .run(args);
        try {
            context.getBean(ClientScenario.class).run();
            System.out.println("monitoring e2e result=passed");
        } finally {
            context.close();
        }
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    ZLinkFrameworkConfigurer clientFramework() {
        return options -> {
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.codecs().addJson();
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/client-flow.log")
                .traceNodeId("java-mon-client");
            options.addClientServerChannel(Contracts.CHANNEL)
                .enableClient(Env.get("ZLINK_JAVA_E2E_API_ENDPOINT"));
        };
    }

    @Bean
    ClientScenario clientScenario(
        systems.zlink.framework.channels.ZLinkClient client,
        ObjectMapper json) {
        return new ClientScenario(client, json);
    }

    public static final class ClientScenario {
        private final systems.zlink.framework.channels.ZLinkClient client;
        private final ObjectMapper json;
        private final HttpClient http = HttpClient.newHttpClient();

        public ClientScenario(
            systems.zlink.framework.channels.ZLinkClient client,
            ObjectMapper json) {
            this.client = client;
            this.json = json;
        }

        public void run() {
            for (int index = 0; index < 5; index++) {
                Contracts.WorkReply reply = client.requestToChannel(
                        Contracts.CHANNEL,
                        new Contracts.WorkRequest("a1-" + index))
                    .timeout(Duration.ofSeconds(3))
                    .await(Contracts.WorkReply.class);
                ensure(reply.value().equals("work:a1-" + index), "reply mismatch");
            }
            waitForEvent(Env.get("ZLINK_JAVA_E2E_REGISTRY_HTTP"), "registry", Set.of(
                "STATUS_CHANGED",
                "TOPOLOGY_CHANGED",
                "SERVICE_SUMMARY_CHANGED"));
            waitForEvent(Env.get("ZLINK_JAVA_E2E_SERVICE_HTTP"), "spot", Set.of(
                "STATUS_CHANGED",
                "PEERS_CHANGED",
                "SUBJECTS_CHANGED"));
            System.out.println("scenario MON-A2 passed");
            System.out.println("scenario MON-A3 passed");
        }

        private void waitForEvent(String baseUrl, String surface, Set<String> expected) {
            long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(20);
            while (System.nanoTime() < deadline) {
                Set<String> observed = events(baseUrl, surface);
                if (observed.containsAll(expected)) {
                    return;
                }
                sleep(200);
            }
            Set<String> observed = events(baseUrl, surface);
            throw new IllegalStateException(
                "missing " + surface + " events " + expected + " at " + baseUrl
                    + "; observed=" + observed + "; evidence=" + get(baseUrl + "/evidence"));
        }

        private Set<String> events(String baseUrl, String surface) {
            try {
                JsonNode root = json.readTree(get(baseUrl + "/evidence")).path("entries");
                Set<String> events = new HashSet<>();
                for (JsonNode entry : root) {
                    if (surface.equals(entry.path("surface").asText())) {
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
}
