package systems.zlink.samples.deliverydispatch.server.tracking;

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
import systems.zlink.samples.deliverydispatch.server.configuration.EvidenceStore;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleLocationStore;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = TrackingServerApplication.class)
public final class TrackingServerApplication {
    private TrackingServerApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(TrackingServerApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer trackingFramework() {
        return options -> {
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(SampleTopology.LogDirectory + "/flow-tracking.log")
                .traceLabel("tracking");
            options.addHandlersFromPackageOf(TrackingServerApplication.class);
            ZLinkSpotNodeBuilder trackingSpot = options.addSpotMesh(SampleNames.CustomerSpotDiscovery);
            trackingSpot.enableRouter(SampleTopology.TrackingSpotEndpoint)
                .setRoutingId(RoutingId.from(SampleNames.TrackingSpotNode));
            trackingSpot.configureEntrySpot()
                .setRoutingId(RoutingId.from(SampleNames.TrackingSpotNode));
            trackingSpot.enablePubSub(SampleTopology.TrackingSpotPubEndpoint);
            options.addClientServerChannel(SampleNames.TrackingChannel)
                .enableServer(SampleTopology.TrackingChannelEndpoint)
                .setRoutingId(RoutingId.from("delivery-tracking-server"))
                .addHandlerGroup("tracking");
        };
    }

    @Bean(destroyMethod = "close")
    ZLinkRedisLocationStore locationStore() {
        return SampleLocationStore.create();
    }

    @Bean
    EvidenceStore evidenceStore() {
        return new EvidenceStore();
    }
}
