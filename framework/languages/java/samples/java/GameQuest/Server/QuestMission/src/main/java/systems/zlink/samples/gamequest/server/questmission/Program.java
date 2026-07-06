package systems.zlink.samples.gamequest.server.questmission;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.gamequest.server.configuration.SampleLocationStore;
import systems.zlink.samples.gamequest.server.configuration.SampleNames;
import systems.zlink.samples.gamequest.server.configuration.SampleTopology;
import systems.zlink.samples.gamequest.server.questmission.store.QuestStore;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = Program.class)
public class Program {
    public static void main(String[] args) throws Exception {
        AutoCloseable app = run(args);
        QuestStore store = ApplicationContextHolder.store;
        HttpServer http = startHttp(store);
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            http.stop(0);
            try {
                app.close();
            } catch (Exception ignored) {
            }
        }));
        Thread.currentThread().join();
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer questMissionFramework() {
        return options -> {
            options.addHandlersFromPackageOf(Program.class);
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(System.getenv().getOrDefault("GAMEQUEST_LOG_DIR", "logs")
                    + "/flow-" + SampleTopology.missionName() + ".log")
                .traceLabel(SampleTopology.missionName());
            options.addClientServerChannel(SampleNames.questOwnerChannelFor(SampleTopology.missionName()))
                .enableServer(SampleTopology.selectedMissionChannelEndpoint())
                .addHandlerGroup("quest-owner");
        };
    }

    @Bean(destroyMethod = "close")
    ZLinkRedisLocationStore locationStore() {
        return SampleLocationStore.create();
    }

    @Bean(destroyMethod = "close")
    QuestStore questStore() {
        QuestStore store = new QuestStore();
        ApplicationContextHolder.store = store;
        return store;
    }

    private static HttpServer startHttp(QuestStore store) throws IOException {
        ObjectMapper json = new ObjectMapper();
        URI uri = URI.create(SampleTopology.selectedMissionHttpEndpoint());
        HttpServer server = HttpServer.create(new InetSocketAddress(uri.getHost(), uri.getPort()), 0);
        server.createContext("/health", exchange -> writeJson(exchange, json, 200, new Health("ok")));
        server.createContext("/self-check/owner/", exchange -> {
            String[] parts = exchange.getRequestURI().getPath().split("/");
            String playerId = parts.length >= 4 ? parts[3] : "";
            store.markRehydrated(playerId);
            writeJson(exchange, json, 200, new CloseOwner(true, true));
        });
        server.createContext("/self-check/events", exchange -> writeJson(exchange, json, 200, store.events()));
        server.start();
        return server;
    }

    private static void writeJson(HttpExchange exchange, ObjectMapper json, int status, Object body)
        throws IOException {
        byte[] bytes = json.writeValueAsString(body).getBytes(StandardCharsets.UTF_8);
        exchange.getResponseHeaders().add("content-type", "application/json");
        exchange.sendResponseHeaders(status, bytes.length);
        exchange.getResponseBody().write(bytes);
        exchange.close();
    }

    private record Health(String status) {
    }

    private record CloseOwner(boolean closed, boolean owner) {
    }

    private static final class ApplicationContextHolder {
        private static QuestStore store;
    }
}
