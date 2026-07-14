package systems.zlink.samples.deliverydispatch.server.customergateway;

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
import systems.zlink.samples.deliverydispatch.server.customergateway.sessions.CustomerSession;
import systems.zlink.samples.deliverydispatch.server.customergateway.spots.CustomerEntrySpot;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = CustomerGatewayApplication.class)
public final class CustomerGatewayApplication {
    private CustomerGatewayApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(CustomerGatewayApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer customerGatewayFramework() {
        return options -> {
            options.addHandlersFromPackageOf(CustomerGatewayApplication.class);
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(SampleTopology.LogDirectory + "/flow-customer-gateway.log")
                .traceLabel("customer-gateway");
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.CustomerSpotDiscovery);
            node.enableRouter(SampleTopology.CustomerSpotRouterEndpoint)
                .setRoutingId(RoutingId.from(SampleTopology.CustomerSpotNodeRid));
            node.configureEntrySpot()
                .setRoutingId(RoutingId.from(SampleTopology.CustomerSpotNodeRid));
            node.enablePubSub(SampleTopology.CustomerSpotEndpoint);
            node.addEntrySpot(CustomerEntrySpot.class);
            node.addActorFactory(SampleNames.CustomerActorType, CustomerActorFactory.class);
            options.addStreamNode(SampleNames.CustomerStreamNode)
                .bind(SampleTopology.CustomerStreamEndpoint)
                .registerSession(CustomerSession.class);
        };
    }

    @Bean(destroyMethod = "close")
    ZLinkRedisLocationStore locationStore() {
        return SampleLocationStore.create();
    }

    @Bean
    CustomerActorDirectory customerActorDirectory() {
        return new CustomerActorDirectory();
    }
}
