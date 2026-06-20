package systems.zlink.samples.bingo.server.play;

import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.json.JsonMapper;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;
import systems.zlink.samples.bingo.server.play.adapters.zlink.actors.PlayerActorFactory;
import systems.zlink.samples.bingo.server.play.adapters.zlink.notifications.BingoNotificationPublisher;
import systems.zlink.samples.bingo.server.play.adapters.zlink.spots.BingoRoomSpot;
import systems.zlink.samples.bingo.server.play.adapters.zlink.spots.handlers.BingoRoomSpotCreatedHandler;
import systems.zlink.samples.bingo.server.play.adapters.zlink.spots.BingoEntrySpot;
import systems.zlink.samples.bingo.server.play.adapters.zlink.handlers.BingoMatchQueue;
import systems.zlink.samples.bingo.server.play.adapters.zlink.handlers.BingoRoomDirectory;
import systems.zlink.samples.bingo.server.play.adapters.zlink.handlers.RedisBingoMatchQueue;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
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
            options.codecs().addJson();
            options.codecs().use(ZLinkProtobufCodec.defaultCodec());
            options.addHandlersFromPackageOf(PlayServerApplication.class);
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient();
            RouteMeshChannelBuilder route = options.addRouteMeshChannel(SampleNames.PlayChannel);
            route.enableServer(SampleTopology.selectedPlayRouteEndpoint());
            route.enableClient(SampleTopology.peerPlayRouteEndpoint());
            route.enableClient(SampleTopology.SessionAPlayRouteEndpoint);
            route.enableClient(SampleTopology.SessionBPlayRouteEndpoint);
            route.addHandlerGroup("play-route");
            route.configureRouting().setRoutingId(RoutingId.from(SampleTopology.selectedPlayNodeRid()));
            options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory.class);
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.RoomSpotDiscovery)
                .useRegistrySpotResolver()
                .addNode(SampleTopology.selectedPlayNodeRid());
            node.enableRouter(SampleTopology.selectedPlaySpotRouterEndpoint())
                .setRouterRoutingId(RoutingId.from(SampleTopology.selectedPlayNodeRid()))
                .connectRouter(SampleTopology.SessionARouterEndpoint)
                .connectRouter(SampleTopology.SessionBRouterEndpoint);
            node.enablePubSub(SampleTopology.selectedPlaySpotEndpoint())
                .setPubSubRoutingId(RoutingId.from(SampleTopology.selectedPlayNodeRid()));
            node.attachChannelClient(SampleNames.ApiChannel);
            node.acceptSpotRoutesFromChannel(
                SampleNames.PlayChannel,
                SampleTopology.selectedPlayRouteEndpoint());
            node.addEntrySpot(BingoEntrySpot.class);
            node.addSpotFactory(BingoRoomSpot.class);
        };
    }

    @Bean
    BingoRoomDirectory bingoRoomDirectory(
        ObjectProvider<ZLinkSpotManager> spots,
        ObjectMapper json,
        BingoMatchQueue matchQueue) {
        return new BingoRoomDirectory(spots.getObject(), json, matchQueue);
    }

    @Bean
    BingoMatchQueue redisBingoMatchQueue() {
        return new RedisBingoMatchQueue();
    }

    @Bean
    BingoNotificationPublisher bingoNotificationPublisher() {
        return new BingoNotificationPublisher();
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
