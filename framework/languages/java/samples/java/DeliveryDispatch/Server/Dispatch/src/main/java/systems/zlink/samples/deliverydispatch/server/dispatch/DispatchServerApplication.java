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
            options.addHandlersFromPackageOf(DispatchServerApplication.class);
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(SampleTopology.LogDirectory + "/flow-dispatch.log")
                .traceLabel("dispatch");
            options.addClientServerChannel(SampleNames.CourierChannel)
                .enableClient();
            // The courier's decision comes back here as its own one-way message, so dispatch has
            // to be a channel server (common sample spec section 7.4).
            options.addClientServerChannel(SampleNames.DispatchChannel)
                .enableServer(SampleTopology.DispatchChannelEndpoint)
                .setRoutingId(RoutingId.from("delivery-dispatch-channel"))
                .addHandlerGroup(SampleNames.DispatchChannel);
            options.addClientServerChannel(SampleNames.TrackingChannel)
                .enableClient()
                .setRoutingId(RoutingId.from("delivery-dispatch-tracking-client"));
            ZLinkSpotNodeBuilder courierRoutes = options.addSpotMesh(SampleNames.CourierSpotDiscovery);
            courierRoutes
                .enableRouter("inproc://deliverydispatch-dispatch-courier-client")
                .setRoutingId(RoutingId.from("deliverydispatch-dispatch-courier-client"));
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
    DeliveryOfferStore deliveryOfferStore() {
        return new DeliveryOfferStore();
    }

    @Bean
    DispatchWorker dispatchWorker(
        systems.zlink.framework.channels.ZLinkClient channels,
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        systems.zlink.framework.spots.SpotHandleResolver spotHandles,
        DeliveryOfferStore offers) {
        return new DispatchWorker(channels, routes, spotHandles, offers);
    }

    @Bean(destroyMethod = "close")
    OfferDeadlineSweeper offerDeadlineSweeper(DeliveryOfferStore offers, DispatchWorker worker) {
        return new OfferDeadlineSweeper(offers, worker);
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
