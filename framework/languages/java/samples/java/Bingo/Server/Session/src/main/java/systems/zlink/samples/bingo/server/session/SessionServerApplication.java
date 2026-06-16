package systems.zlink.samples.bingo.server.session;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.bingo.server.session.sessions.BingoSession;
import systems.zlink.samples.bingo.server.session.sessions.handlers.AuthenticateSessionHandler;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTopology;



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
            options.codecs().addProtobuf();
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient();
            options.addClientServerChannel(SampleNames.PlayChannel)
                .enableClient();
            RouteMeshChannelBuilder route = options.addRouteMeshChannel(SampleNames.RoomRouteChannel);
            route.enableServer(SampleTopology.SessionRouteEndpoint);
            route.enableClient(SampleTopology.PlayRouteEndpoint);
            route.configureRouting().setRoutingId(RoutingId.from(SampleTopology.SessionRouterRid));
            options.useRegistrySpotRemoteAddresses(SampleNames.RoomSpotDiscovery)
                .setRouterChannelId(SampleNames.RoomRouteChannel);
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.RoomSpotDiscovery)
                .addNode(SampleNames.SessionSpotNode);
            node.enableRouter(SampleTopology.SessionRouterEndpoint)
                .setRouterRoutingId(RoutingId.from(SampleTopology.SessionRouterRid));
            node.enablePubSub(SampleTopology.SessionSpotEndpoint)
                .setPubSubRoutingId(RoutingId.from(SampleTopology.SessionPubRid));
            node.acceptSpotRoutesFromChannel(SampleNames.RoomRouteChannel);
            options.addStreamNode(SampleNames.StreamNode)
                .attachActorGateway(SampleNames.SessionSpotNode)
                .bind(SampleTopology.StreamEndpoint)
                .registerSession(BingoSession.class)
                .addSessionPacketHandler(AuthenticateSessionHandler.class);
        };
    }
}
