package systems.zlink.samples.bingo.server.session;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.bingo.server.session.sessions.BingoSession;
import systems.zlink.samples.bingo.server.session.sessions.handlers.AuthenticateSessionHandler;
import systems.zlink.samples.bingo.shared.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.configuration.SampleTopology;



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
            options.useDiscovery(discovery ->
                discovery.addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint));
            options.codecs().addJson();
            options.addClientServerChannel(SampleNames.ApiChannel, channel -> channel.enableClient());
            options.addClientServerChannel(SampleNames.PlayChannel, channel -> channel.enableClient());
            options.addRouteMeshChannel(SampleNames.RoomRouteChannel, route -> {
                route.bind(SampleTopology.SessionRouteEndpoint);
                route.configureRouting(routing ->
                    routing.setRoutingId(RoutingId.from(SampleTopology.SessionRouterRid)));
                route.useManualConnections(endpoints ->
                    endpoints.connect(SampleTopology.PlayRouteEndpoint));
            });
            options.useRegistrySpotRemoteAddresses(SampleNames.RoomSpotDiscovery, registry ->
                registry.setRouterChannelId(SampleNames.RoomRouteChannel));
            options.addSpotMesh(SampleNames.RoomSpotDiscovery, mesh ->
                mesh.addNode(SampleNames.SessionSpotNode, node -> {
                    node.enableRouter(router -> {
                        router.bindRouter(SampleTopology.SessionRouterEndpoint);
                        router.setRoutingId(RoutingId.from(SampleTopology.SessionRouterRid));
                    });
                    node.enablePubSub(pubSub -> {
                        pubSub.bindPubSub(SampleTopology.SessionSpotEndpoint);
                        pubSub.setRoutingId(RoutingId.from(SampleTopology.SessionPubRid));
                    });
                    node.acceptSpotRoutesFromChannel(SampleNames.RoomRouteChannel);
                }));
            options.addStreamNode(SampleNames.StreamNode, stream -> {
                stream.attachActorGateway(SampleNames.SessionSpotNode);
                stream.bind(SampleTopology.StreamEndpoint);
                stream.registerSession(BingoSession.class);
                stream.addSessionPacketHandler(AuthenticateSessionHandler.class);
            });
        };
    }
}
