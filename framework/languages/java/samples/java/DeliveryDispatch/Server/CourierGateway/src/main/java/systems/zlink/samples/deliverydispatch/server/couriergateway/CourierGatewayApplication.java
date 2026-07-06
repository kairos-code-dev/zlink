package systems.zlink.samples.deliverydispatch.server.couriergateway;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleLocationStore;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = CourierGatewayApplication.class)
public final class CourierGatewayApplication {
    private CourierGatewayApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(CourierGatewayApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer courierGatewayFramework() {
        return options -> {
            options.addHandlersFromPackageOf(CourierGatewayApplication.class);
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(System.getenv().getOrDefault("DELIVERYDISPATCH_LOG_DIR", "logs")
                    + "/flow-courier-gateway.log")
                .traceLabel("courier-gateway");
            options.addClientServerChannel(SampleNames.CourierChannel)
                .enableServer(SampleTopology.CourierGatewayChannelEndpoint)
                .setRoutingId(RoutingId.from("delivery-courier-gateway-server"))
                .addHandlerGroup("courier-gateway");
            options.addClientServerChannel(SampleNames.courierActorNodeChannelFor(SampleTopology.CourierActorNode1Rid))
                .enableClient(SampleTopology.CourierActorNode1RouteEndpoint)
                .setRoutingId(RoutingId.from("delivery-courier-gateway-node1"));
            options.addClientServerChannel(SampleNames.courierActorNodeChannelFor(SampleTopology.CourierActorNode2Rid))
                .enableClient(SampleTopology.CourierActorNode2RouteEndpoint)
                .setRoutingId(RoutingId.from("delivery-courier-gateway-node2"));
        };
    }

    @Bean(destroyMethod = "close")
    ZLinkRedisLocationStore locationStore() {
        return SampleLocationStore.create();
    }

    @Bean
    CourierDirectory courierDirectory() {
        return new CourierDirectory();
    }
}
