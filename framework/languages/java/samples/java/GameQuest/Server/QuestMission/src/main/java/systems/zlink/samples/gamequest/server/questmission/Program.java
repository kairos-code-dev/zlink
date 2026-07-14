package systems.zlink.samples.gamequest.server.questmission;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.core.env.StandardEnvironment;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
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
        SampleTopology topology = app.getBean(SampleTopology.class);
        HttpServer http = startHttp(store, topology);
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
            options.addClientServerChannel(SampleNames.questOwnerChannelFor(mission.instanceName()))
                .enableServer(mission.channelEndpoint())
                .addHandlerGroup("quest-owner");
            options.addClientServerChannel(SampleNames.questNotificationChannelFor("api-a"))
                .enableClient(mission.apiANotificationChannelEndpoint());
            options.addClientServerChannel(SampleNames.questNotificationChannelFor("api-b"))
                .enableClient(mission.apiBNotificationChannelEndpoint());
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.PlayerQuestSpotDiscovery);
            node.enableRouter(mission.spotRouterEndpoint())
                .enablePubSub(mission.spotEndpoint())
                .setRoutingId(mission.routingId());
            node.addSpotFactory(PlayerQuestSpot.class);
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

    private static HttpServer startHttp(QuestStore store, SampleTopology topology) throws IOException {
        ObjectMapper json = new ObjectMapper();
        URI uri = URI.create(topology.questMission().httpEndpoint());
        HttpServer server = HttpServer.create(new InetSocketAddress(uri.getHost(), uri.getPort()), 0);
        server.createContext("/health", exchange -> writeJson(exchange, json, 200, new Health("ok")));
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

    private static final class ApplicationContextHolder {
        private static QuestStore store;
    }
}
