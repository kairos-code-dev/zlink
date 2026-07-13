package systems.zlink.samples.deliverydispatch.server.dispatch;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleLocationStore;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = DispatchServerApplication.class)
public final class DispatchServerApplication {
    private DispatchServerApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(DispatchServerApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer dispatchFramework() {
        return options -> {
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(System.getenv().getOrDefault("DELIVERYDISPATCH_LOG_DIR", "logs")
                    + "/flow-dispatch.log")
                .traceLabel("dispatch");
            options.addClientServerChannel(SampleNames.CourierChannel)
                .enableClient();
            options.addClientServerChannel(SampleNames.TrackingChannel)
                .enableClient()
                .setRoutingId(RoutingId.from("delivery-dispatch-tracking-client"));
            ZLinkSpotNodeBuilder courierRoutes = options.addSpotMesh(SampleNames.CourierSpotDiscovery);
            courierRoutes
                .enableRouter("inproc://deliverydispatch-dispatch-courier-client")
                .setRoutingId(RoutingId.from("deliverydispatch-dispatch-courier-client"));
            courierRoutes.connectRouter(
                RoutingId.from(SampleTopology.CourierActorNode1Rid),
                SampleTopology.CourierActorNode1RouterEndpoint);
            courierRoutes.connectRouter(
                RoutingId.from(SampleTopology.CourierActorNode2Rid),
                SampleTopology.CourierActorNode2RouterEndpoint);
        };
    }

    @Bean(destroyMethod = "close")
    ZLinkRedisLocationStore locationStore() {
        return SampleLocationStore.create();
    }

    @Bean
    DispatchWorkQueue dispatchWorkQueue(DispatchWorker worker) {
        return new DispatchWorkQueue(worker);
    }

    @Bean
    DispatchWorker dispatchWorker(
        systems.zlink.framework.channels.ZLinkClient channels,
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        systems.zlink.framework.spots.SpotHandleResolver spotHandles) {
        return new DispatchWorker(channels, routes, spotHandles);
    }

    @Bean
    DispatchHttpServer dispatchHttpServer(
        ObjectMapper json,
        DispatchWorkQueue queue) throws IOException {
        return new DispatchHttpServer(json, queue);
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }
}
