package systems.zlink.samples.shoppingmall.server.orderworkflow;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.SpotHandleResolver;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.shoppingmall.server.configuration.SampleFlowLog;
import systems.zlink.samples.shoppingmall.server.configuration.SampleLocationStore;
import systems.zlink.samples.shoppingmall.server.configuration.SampleNames;
import systems.zlink.samples.shoppingmall.server.configuration.SampleTopology;
import systems.zlink.samples.shoppingmall.server.orderworkflow.spots.OrderWorkflowSpot;
import systems.zlink.samples.shoppingmall.server.shared.store.RedisCommerceStore;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = Program.class)
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
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer orderWorkflowFramework() {
        return options -> {
            options.addHandlersFromPackageOf(Program.class);
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(SampleFlowLog.path(SampleTopology.workflowName()))
                .traceLabel(SampleTopology.workflowName());
            options.addClientServerChannel(SampleNames.orderWorkflowChannelFor(SampleTopology.workflowName()))
                .enableServer(SampleTopology.selectedWorkflowChannelEndpoint())
                .addHandlerGroup("order-workflow");
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.OrderSpotDiscovery);
            node.enableRouter(SampleTopology.selectedWorkflowSpotRouterEndpoint())
                .enablePubSub(SampleTopology.selectedWorkflowSpotEndpoint())
                .setRoutingId(SampleTopology.selectedWorkflowRoutingId());
            node.addSpotFactory(OrderWorkflowSpot.class);
        };
    }

    @Bean(destroyMethod = "close")
    ZLinkRedisLocationStore locationStore() {
        return SampleLocationStore.create();
    }

    @Bean(destroyMethod = "close")
    RedisCommerceStore redisCommerceStore() {
        RedisCommerceStore store = new RedisCommerceStore();
        store.seedDefaults();
        return store;
    }

    @Bean
    OrderWorkflowService orderWorkflowService(
        RedisCommerceStore store,
        ZLinkSpotManager spots,
        ZLinkRouteClient routes,
        SpotHandleResolver spotHandles) {
        return new OrderWorkflowService(store, spots, routes, spotHandles);
    }

    private static HttpServer startHttp() throws IOException {
        ObjectMapper json = new ObjectMapper();
        URI uri = URI.create(SampleTopology.selectedWorkflowHttpUrl());
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
