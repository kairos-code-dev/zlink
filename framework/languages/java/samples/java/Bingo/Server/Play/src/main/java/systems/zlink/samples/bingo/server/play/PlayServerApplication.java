package systems.zlink.samples.bingo.server.play;

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
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.actors.PlayerActorFactory;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.matchmaking.RedisBingoMatchQueue;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.BingoRoomSpot;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.handlers.BingoRoomSpotCreatedHandler;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.BingoEntrySpot;
import systems.zlink.samples.bingo.server.play.application.roomallocation.BingoMatchQueue;
import systems.zlink.samples.bingo.server.play.application.roomallocation.BingoRoomAllocator;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTimings;
import systems.zlink.samples.bingo.server.configuration.SampleTopology;



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = PlayServerApplication.class)
public final class PlayServerApplication {
    private PlayServerApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(PlayServerApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer playFramework() {
        return options -> {
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(System.getenv().getOrDefault("BINGO_LOG_DIR", "logs") + "/flow-play.log")
                .traceNodeId("play");
            options.codecs().addJson();
            options.codecs().use(ZLinkProtobufCodec.defaultCodec());
            options.addHandlersFromPackageOf(PlayServerApplication.class);
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient();
            RouteMeshChannelBuilder route = options.addRouteMesh(SampleNames.PlayChannel);
            route.enableServer(SampleTopology.selectedPlayRouteEndpoint());
            route.enableClient();
            route.addHandlerGroup("play-route");
            route.setRoutingId(RoutingId.from(SampleTopology.selectedPlayNodeRid()));
            options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory.class);
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.RoomSpotDiscovery);
            options.useRegistrySpotRemoteAddresses(SampleNames.RoomSpotDiscovery)
                .setRouterChannelId(SampleNames.PlayChannel);
            node.enableRouter(SampleTopology.selectedPlaySpotRouterEndpoint())
                .setRouterRoutingId(RoutingId.from(SampleTopology.selectedPlayNodeRid()));
            node.enablePubSub(SampleTopology.selectedPlaySpotEndpoint())
                .setPubSubRoutingId(RoutingId.from(SampleTopology.selectedPlayNodeRid()));
            node.addEntrySpot(BingoEntrySpot.class);
            node.addSpotFactory(BingoRoomSpot.class);
        };
    }

    @Bean
    BingoRoomAllocator bingoRoomAllocator(BingoMatchQueue matchQueue) {
        return new BingoRoomAllocator(matchQueue, SampleTimings.DrawPeriod.toMillis());
    }

    @Bean
    BingoMatchQueue redisBingoMatchQueue() {
        return new RedisBingoMatchQueue();
    }

    @Bean
    BingoRoomSpotCreatedHandler bingoRoomSpotCreatedHandler(ObjectMapper json) {
        return new BingoRoomSpotCreatedHandler(json);
    }

    @Bean
    ObjectMapper bingoJsonMapper() {
        return JsonMapper.builder()
            .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
            .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
            .findAndAddModules()
            .build();
    }
}
