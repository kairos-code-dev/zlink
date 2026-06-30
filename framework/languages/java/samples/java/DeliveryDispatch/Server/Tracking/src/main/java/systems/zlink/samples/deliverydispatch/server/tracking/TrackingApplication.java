package systems.zlink.samples.deliverydispatch.server.tracking;

import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.json.JsonMapper;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.deliverydispatch.server.configuration.EvidenceStore;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;
import systems.zlink.samples.deliverydispatch.server.tracking.actors.CustomerActorFactory;
import systems.zlink.samples.deliverydispatch.server.tracking.spots.entryspot.CustomerEntrySpot;
import systems.zlink.samples.deliverydispatch.server.tracking.spots.deliverytrackingspot.DeliverySpotDirectory;
import systems.zlink.samples.deliverydispatch.server.tracking.spots.deliverytrackingspot.DeliveryTrackingSpot;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = TrackingApplication.class)
public final class TrackingApplication {
    private TrackingApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(TrackingApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    EvidenceStore evidenceStore() {
        return new EvidenceStore();
    }

    @Bean
    DeliverySpotDirectory deliverySpotDirectory() {
        return new DeliverySpotDirectory();
    }

    @Bean
    ObjectMapper deliveryDispatchJsonMapper() {
        return JsonMapper.builder()
            .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
            .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
            .findAndAddModules()
            .build();
    }

    @Bean
    ZLinkFrameworkConfigurer trackingFramework() {
        return options -> {
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(System.getenv().getOrDefault("DELIVERYDISPATCH_LOG_DIR", "logs") + "/flow-tracking.log")
                .traceLabel("tracking");
            options.addHandlersFromPackageOf(TrackingApplication.class);
            options.addClientServerChannel(SampleNames.TrackingChannel)
                .enableServer(SampleTopology.TrackingChannelEndpoint)
                .addHandlerGroup("tracking");
            options.addFanoutChannel(SampleNames.StatusFanoutChannel)
                .enablePublisher(SampleTopology.StatusFanoutEndpoint);
            RouteMeshChannelBuilder route = options.addRouteMesh(SampleNames.SpotRouteChannel);
            route.enableServer(SampleTopology.TrackingSpotRouteEndpoint);
            route.enableClient();
            route.setRoutingId(RoutingId.from(SampleTopology.TrackingSpotNodeRid));
            options.useRegistrySpotRemoteAddresses(SampleNames.DeliverySpotDiscovery)
                .setRouterChannelId(SampleNames.SpotRouteChannel);
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.DeliverySpotDiscovery)
                ;
            node.enableRouter(SampleTopology.TrackingSpotRouterEndpoint)
                .setRoutingId(RoutingId.from(SampleTopology.TrackingSpotNodeRid));
            node.enablePubSub(SampleTopology.TrackingSpotEndpoint);node.addEntrySpot(CustomerEntrySpot.class);
            node.addSpotFactory(DeliveryTrackingSpot.class);
            node.addActorFactory(SampleNames.CustomerActorType, CustomerActorFactory.class);
        };
    }
}
