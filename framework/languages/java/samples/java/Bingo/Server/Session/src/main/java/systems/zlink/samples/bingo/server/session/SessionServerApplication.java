package systems.zlink.samples.bingo.server.session;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.samples.bingo.server.configuration.SampleLocationStore;
import systems.zlink.samples.bingo.server.session.sessions.BingoSession;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTopology;
import systems.zlink.samples.bingo.server.configuration.BingoMetricsReporter;
import io.micrometer.core.instrument.MeterRegistry;



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
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(SampleTopology.LogDirectory + "/flow-session.log")
                .traceLabel("session");
            options.codecs().use(ZLinkProtobufCodec.defaultCodec());
            options.configureLocations();
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient();
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.RoomSpotDiscovery);
            node.enableRouter(SampleTopology.selectedSessionRouterEndpoint())
                .setRoutingId(RoutingId.from(SampleTopology.selectedSessionRouterRid()));
            node.enablePubSub(SampleTopology.selectedSessionSpotEndpoint());
            options.addStreamNode(SampleNames.StreamNode)
                .bind(SampleTopology.selectedStreamEndpoint())
                .registerSession(BingoSession.class);
        };
    }

    @Bean
    ZLinkRedisLocationStore locationStore() {
        return SampleLocationStore.create();
    }

    @Bean(destroyMethod = "close")
    BingoMetricsReporter bingoMetricsReporter(MeterRegistry registry) {
        return new BingoMetricsReporter(registry, "session");
    }
}
