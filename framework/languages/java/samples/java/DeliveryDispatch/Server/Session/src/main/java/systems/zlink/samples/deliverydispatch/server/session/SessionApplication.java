package systems.zlink.samples.deliverydispatch.server.session;

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
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;
import systems.zlink.samples.deliverydispatch.server.session.sessions.CustomerSession;
import systems.zlink.samples.deliverydispatch.server.session.sessions.CustomerSessionDirectory;
import systems.zlink.samples.deliverydispatch.server.session.sessions.handlers.DeliveryStatusFanoutHandler;
import systems.zlink.samples.deliverydispatch.server.session.sessions.handlers.SubscribeDeliveryHandler;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = SessionApplication.class)
public final class SessionApplication {
    private SessionApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(SessionApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    CustomerSessionDirectory customerSessionDirectory() {
        return new CustomerSessionDirectory();
    }

    @Bean
    ZLinkFrameworkConfigurer sessionFramework() {
        return options -> {
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(System.getenv().getOrDefault("DELIVERYDISPATCH_LOG_DIR", "logs") + "/flow-session.log")
                .traceNodeId("session");
            options.codecs().addJson();
            options.addHandlersFromPackageOf(SessionApplication.class);
            options.addClientServerChannel(SampleNames.TrackingChannel)
                .enableClient();
            options.addFanoutChannel(SampleNames.StatusFanoutChannel)
                .enableSubscriber()
                .addPublishHandler(
                    DeliveryStatusFanoutHandler.class,
                    Messages.DeliveryStatusNotify.class,
                    "DeliveryStatusNotify");
            RouteMeshChannelBuilder route = options.addRouteMeshChannel(SampleNames.SpotRouteChannel);
            route.enableServer(SampleTopology.SessionSpotRouteEndpoint);
            route.enableClient();
            route.setRoutingId(RoutingId.from(SampleTopology.SessionSpotNodeRid));
            options.useRegistrySpotRemoteAddresses(SampleNames.DeliverySpotDiscovery)
                .setRouterChannelId(SampleNames.SpotRouteChannel);
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.DeliverySpotDiscovery)
                .addNode(SampleNames.SessionSpotNode);
            node.enableRouter(SampleTopology.SessionSpotRouterEndpoint)
                .setRouterRoutingId(RoutingId.from(SampleTopology.SessionSpotNodeRid));
            node.enablePubSub(SampleTopology.SessionSpotEndpoint)
                .setPubSubRoutingId(RoutingId.from(SampleTopology.SessionSpotPubRid));
            node.acceptSpotRoutesFromChannel(SampleNames.SpotRouteChannel);
            options.addStreamNode(SampleNames.CustomerStreamNode)
                .attachActorGateway(SampleNames.SessionSpotNode)
                .bind(SampleTopology.SessionStreamEndpoint)
                .registerSession(CustomerSession.class)
                .addSessionPacketHandler(SubscribeDeliveryHandler.class);
        };
    }
}
