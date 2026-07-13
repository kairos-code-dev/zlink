package systems.zlink.samples.supportchat.server.support;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import systems.zlink.contracts.core.RoutingId;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.supportchat.server.configuration.SampleFlowLog;
import systems.zlink.samples.supportchat.server.configuration.SampleLocationStore;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.configuration.SampleTopology;
import systems.zlink.samples.supportchat.server.support.actors.SupportActorDirectory;
import systems.zlink.samples.supportchat.server.support.actors.SupportUserActorFactory;
import systems.zlink.samples.supportchat.server.support.actors.SupportUserActorTransferAdapter;
import systems.zlink.samples.supportchat.server.support.application.AgentAssignmentService;
import systems.zlink.samples.supportchat.server.support.application.ConversationAllocator;
import systems.zlink.samples.supportchat.server.support.application.ConversationSpotFactory;
import systems.zlink.samples.supportchat.server.support.infrastructure.FrameworkConversationSpotFactory;
import systems.zlink.samples.supportchat.server.support.infrastructure.ConversationNotificationPublisher;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.supportchat.server.support.spots.conversationspot.ConversationSpot;
import systems.zlink.samples.supportchat.server.support.spots.entryspot.SupportEntrySpot;

@EnableZLinkFramework
@SpringBootApplication(proxyBeanMethods = false, scanBasePackageClasses = Program.class)
public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        AutoCloseable app = run(args);
        HttpServer http = startHttp();
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
    ZLinkFrameworkConfigurer supportFramework() {
        return options -> {
            options.configureLocations();
            options.addHandlersFromPackageOf(Program.class);
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(SampleFlowLog.path("support"))
                .traceLabel("support");
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient();
            options.addClientServerChannel(SampleNames.SupportChannel)
                .enableServer(SampleTopology.SupportChannelEndpoint)
                .addHandlerGroup(SampleNames.SupportChannel);
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.SupportActorMesh);
            node.enableRouter(SampleTopology.SupportSpotRouterEndpoint)
                .setRoutingId(RoutingId.from(SampleTopology.SupportSpotNodeRid));
            node.connectRouter(
                RoutingId.from(SampleTopology.SessionSpotNodeRid),
                SampleTopology.SessionSpotRouterEndpoint);
            node.configureEntrySpot().setRoutingId(RoutingId.from(SampleTopology.SupportSpotNodeRid));
            node.addEntrySpot(SupportEntrySpot.class);
            node.addActorFactory(SampleNames.SupportActorType, SupportUserActorFactory.class);
            node.addActorTransferAdapter(
                SampleNames.SupportActorType,
                SupportUserActorTransferAdapter.class);
            node.addSpotFactory(ConversationSpot.class);
        };
    }

    @Bean(destroyMethod = "close")
    ZLinkRedisLocationStore locationStore() {
        return SampleLocationStore.create();
    }

    @Bean
    AgentAssignmentService agentAssignmentService() {
        return new AgentAssignmentService(SampleNames.AgentCapacity);
    }

    @Bean
    ConversationSpotFactory conversationSpotFactory(ZLinkSpotManager spots) {
        return new FrameworkConversationSpotFactory(spots);
    }

    @Bean
    ConversationAllocator conversationAllocator(ConversationSpotFactory spots) {
        return new ConversationAllocator(spots);
    }

    @Bean
    ConversationNotificationPublisher conversationNotificationPublisher() {
        return new ConversationNotificationPublisher();
    }

    @Bean
    SupportActorDirectory supportActorDirectory() {
        return new SupportActorDirectory();
    }

    private static HttpServer startHttp() throws IOException {
        ObjectMapper json = new ObjectMapper();
        URI uri = URI.create(SampleTopology.SupportHttpEndpoint);
        HttpServer server = HttpServer.create(new InetSocketAddress(uri.getHost(), uri.getPort()), 0);
        server.createContext("/health", exchange -> {
            byte[] bytes = json.writeValueAsString(new Health("ok")).getBytes(StandardCharsets.UTF_8);
            exchange.getResponseHeaders().add("content-type", "application/json");
            exchange.sendResponseHeaders(200, bytes.length);
            exchange.getResponseBody().write(bytes);
            exchange.close();
        });
        server.start();
        return server;
    }

    private record Health(String status) {
    }
}
