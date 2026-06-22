package systems.zlink.samples.supportchat.server.session;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.configuration.SampleTopology;
import systems.zlink.samples.supportchat.server.session.sessions.SupportChatSession;
import systems.zlink.samples.supportchat.server.session.sessions.handlers.AuthenticateSupportChatSessionHandler;



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = SessionServerApplication.class)
public final class SessionServerApplication {
    private SessionServerApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(SessionServerApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer sessionFramework() {
        return options -> {
            options.addHandlersFromPackageOf(SessionServerApplication.class);
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            options.codecs().use(ZLinkProtobufCodec.defaultCodec());
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient();
            options.addClientServerChannel(SampleNames.SupportChannel)
                .enableClient();
            RouteMeshChannelBuilder route = options.addRouteMeshChannel(SampleNames.SupportRouteChannel);
            route.enableServer(SampleTopology.SessionRouteEndpoint);
            route.enableClient(SampleTopology.SupportRouteEndpoint);
            route.setRoutingId(RoutingId.from(SampleTopology.SessionRouterRid));
            options.useRegistrySpotRemoteAddresses(SampleNames.SupportSpotDiscovery)
                .setRouterChannelId(SampleNames.SupportRouteChannel);
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.SupportSpotDiscovery)
                .addNode(SampleNames.SessionSpotNode);
            node.enableRouter(SampleTopology.SessionRouterEndpoint)
                .setRouterRoutingId(RoutingId.from(SampleTopology.SessionRouterRid));
            node.enablePubSub(SampleTopology.SessionSpotEndpoint)
                .setPubSubRoutingId(RoutingId.from(SampleTopology.SessionPubRid));
            node.acceptSpotRoutesFromChannel(SampleNames.SupportRouteChannel);
            options.addStreamNode(SampleNames.StreamNode)
                .attachActorGateway(SampleNames.SessionSpotNode)
                .bind(SampleTopology.StreamEndpoint)
                .registerSession(SupportChatSession.class)
                .addSessionPacketHandler(AuthenticateSupportChatSessionHandler.class);
        };
    }
}
