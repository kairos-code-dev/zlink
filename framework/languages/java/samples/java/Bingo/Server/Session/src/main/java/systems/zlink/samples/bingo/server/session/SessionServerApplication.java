package systems.zlink.samples.bingo.server.session;

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
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;
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
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(System.getenv().getOrDefault("BINGO_LOG_DIR", "logs") + "/flow-session.log")
                .traceLabel("session");
            options.codecs().use(ZLinkProtobufCodec.defaultCodec());
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient();
            RouteMeshChannelBuilder route = options.addRouteMesh(SampleNames.PlayChannel);
            route.enableServer(SampleTopology.selectedSessionRouteEndpoint());
            route.enableClient();
            route.setRoutingId(RoutingId.from(SampleTopology.selectedSessionRouteRid()));
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.RoomSpotDiscovery);
            node.enableRouter(SampleTopology.selectedSessionRouterEndpoint())
                .setRoutingId(RoutingId.from(SampleTopology.selectedSessionRouterRid()));
            options.addStreamNode(SampleNames.StreamNode)
                .bind(SampleTopology.selectedStreamEndpoint())
                .registerSession(BingoSession.class)
                .addSessionPacketHandler(AuthenticateSessionHandler.class);
        };
    }
}
