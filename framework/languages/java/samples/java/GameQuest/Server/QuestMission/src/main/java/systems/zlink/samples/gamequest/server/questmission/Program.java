package systems.zlink.samples.gamequest.server.questmission;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import systems.zlink.contracts.core.RoutingId;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.core.env.StandardEnvironment;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkMeshNodeBuilder;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.spots.SpotHandleResolver;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.gamequest.server.configuration.SampleLocationStore;
import systems.zlink.samples.gamequest.server.configuration.SampleNames;
import systems.zlink.samples.gamequest.server.configuration.SampleTopology;
import systems.zlink.samples.gamequest.server.questmission.store.QuestStore;
import systems.zlink.samples.gamequest.server.questmission.spots.PlayerQuestSpot;
import systems.zlink.samples.gamequest.server.questmission.spots.PlayerQuestRouter;

@EnableZLinkFramework
@EnableConfigurationProperties(SampleTopology.class)
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = Program.class)
public class Program {
    public static void main(String[] args) throws Exception {
        ConfigurableApplicationContext app = run(SampleTopology.configPath(args));
        QuestStore store = app.getBean(QuestStore.class);
        ZLinkSpotManager spots = app.getBean(ZLinkSpotManager.class);
        SampleTopology topology = app.getBean(SampleTopology.class);
        HttpServer http = startHttp(store, spots, topology);
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            http.stop(0);
            try {
                app.close();
            } catch (Exception ignored) {
            }
        }));
        Thread.currentThread().join();
    }

    public static ConfigurableApplicationContext run(String configPath) {
        StandardEnvironment environment = new StandardEnvironment();
        environment.getPropertySources().remove(StandardEnvironment.SYSTEM_ENVIRONMENT_PROPERTY_SOURCE_NAME);
        environment.getPropertySources().remove(StandardEnvironment.SYSTEM_PROPERTIES_PROPERTY_SOURCE_NAME);
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .environment(environment)
            .properties("spring.config.location=" + Path.of(configPath).toAbsolutePath().toUri())
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run();
    }

    @Bean
    ZLinkFrameworkConfigurer questMissionFramework(SampleTopology topology) {
        SampleTopology.QuestMission mission = topology.questMission();
        return options -> {
            options.addHandlersFromPackageOf(Program.class);
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(mission.logDirectory() + "/flow-" + mission.instanceName() + ".log")
                .traceLabel(mission.instanceName());
            options.addClientServerChannel(SampleNames.QuestOwnerChannel)
                .enableServer(mission.channelEndpoint())
                .addHandlerGroup("quest-owner");
            options.addClientServerChannel(SampleNames.questNotificationChannelFor("api-a"))
                .enableClient();
            options.addClientServerChannel(SampleNames.questNotificationChannelFor("api-b"))
                .enableClient();
            ZLinkMeshNodeBuilder node = options.addRouteMesh(SampleNames.PlayerQuestSpotDiscovery);
            node.listen(mission.spotRouterEndpoint())
                .useAllocatedRoutingId(16, "gamequest-mission-owner");
            node.objects()
                .server()
                .addSpotFactory(
                    "gamequest.player-quest",
                    PlayerQuestSpot.class,
                    factory -> factory.disableRelocation());
        };
    }

    @Bean(destroyMethod = "close")
    ZLinkRedisLocationStore locationStore(SampleTopology topology) {
        return SampleLocationStore.create(topology);
    }

    @Bean(destroyMethod = "close")
    QuestStore questStore(SampleTopology topology) {
        QuestStore store = new QuestStore(topology);
        ApplicationContextHolder.store = store;
        return store;
    }

    @Bean
    PlayerQuestRouter playerQuestRouter(
        ZLinkSpotManager spots,
        ZLinkRouteClient routes,
        SpotHandleResolver handles) {
        return new PlayerQuestRouter(spots, routes, handles);
    }

    private static HttpServer startHttp(
        QuestStore store,
        ZLinkSpotManager spots,
        SampleTopology topology) throws IOException {
        ObjectMapper json = new ObjectMapper();
        URI uri = URI.create(topology.questMission().httpEndpoint());
        HttpServer server = HttpServer.create(new InetSocketAddress(uri.getHost(), uri.getPort()), 0);
        server.createContext("/health", exchange -> writeJson(exchange, json, 200, new Health("ok")));
        server.createContext("/self-check/events", exchange -> writeJson(exchange, json, 200, store.events()));
        server.createContext("/self-check/owner/", exchange -> {
            String path = exchange.getRequestURI().getPath();
            String suffix = path.substring("/self-check/owner/".length());
            String[] parts = suffix.split("/");
            if (parts.length != 2 || !"close".equals(parts[1])) {
                writeJson(exchange, json, 404, new ErrorBody("unknown owner operation"));
                return;
            }
            spots.close(RoutingId.from(parts[0])).whenComplete((closed, error) -> {
                if (error != null) {
                    writeJsonUnchecked(exchange, json, 500, new ErrorBody(error.getMessage()));
                    return;
                }
                writeJsonUnchecked(exchange, json, closed ? 200 : 404, new OwnerClosed(closed));
            });
        });
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

    private static void writeJsonUnchecked(
        HttpExchange exchange,
        ObjectMapper json,
        int status,
        Object body) {
        try {
            writeJson(exchange, json, status, body);
        } catch (IOException error) {
            throw new java.util.concurrent.CompletionException(error);
        }
    }

    private record Health(String status) {
    }

    private record OwnerClosed(boolean closed) {
    }

    private record ErrorBody(String error) {
    }

    private static final class ApplicationContextHolder {
        private static QuestStore store;
    }
}
