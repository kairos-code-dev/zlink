package systems.zlink.samples.deliverydispatch.server.couriersession;

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
import systems.zlink.samples.deliverydispatch.server.couriersession.sessions.CourierSession;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = CourierSessionApplication.class)
public final class CourierSessionApplication {
    private CourierSessionApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(CourierSessionApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer courierSessionFramework() {
        return options -> {
            options.addHandlersFromPackageOf(CourierSessionApplication.class);
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(SampleTopology.LogDirectory + "/flow-courier-session.log")
                .traceLabel("courier-session");
            options.addClientServerChannel(SampleNames.CourierChannel)
                .enableClient();
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.CourierSpotDiscovery);
            node.enableRouter(SampleTopology.CourierSessionSpotRouterEndpoint)
                .setRoutingId(RoutingId.from(SampleTopology.CourierSessionSpotNodeRid));
            node.enablePubSub(SampleTopology.CourierSessionSpotEndpoint);
            options.addStreamNode(SampleNames.CourierStreamNode)
                .bind(SampleTopology.CourierStreamEndpoint)
                .registerSession(CourierSession.class);
        };
    }

    @Bean(destroyMethod = "close")
    ZLinkRedisLocationStore locationStore() {
        return SampleLocationStore.create();
    }
}
