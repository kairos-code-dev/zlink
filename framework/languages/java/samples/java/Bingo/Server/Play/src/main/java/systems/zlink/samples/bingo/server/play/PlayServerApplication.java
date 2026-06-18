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
import systems.zlink.samples.bingo.server.play.adapters.zlink.handlers.BingoRoomDirectory;
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
            options.codecs().use(ZLinkProtobufCodec.defaultCodec());
            options.addHandlersFromPackageOf(PlayServerApplication.class);
            options.addClientServerChannel(SampleNames.PlayChannel)
                .enableServer(SampleTopology.PlayChannelEndpoint)
                .addHandlerGroup("play");
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient();
            RouteMeshChannelBuilder route = options.addRouteMeshChannel(SampleNames.RoomRouteChannel);
            route.enableServer(SampleTopology.PlayRouteEndpoint);
            route.enableClient(SampleTopology.SessionRouteEndpoint);
            route.configureRouting().setRoutingId(RoutingId.from(SampleTopology.PlayRid));
            options.useRegistrySpotRemoteAddresses(SampleNames.RoomSpotDiscovery)
                .setRouterChannelId(SampleNames.RoomRouteChannel);
            options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory.class);
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.RoomSpotDiscovery)
                .addNode(SampleNames.RoomSpotNode);
            node.enableRouter(SampleTopology.PlaySpotRouterEndpoint)
                .setRouterRoutingId(RoutingId.from(SampleTopology.PlayRid));
            node.enablePubSub(SampleTopology.PlaySpotEndpoint);
            node.attachChannelClient(SampleNames.ApiChannel);
            node.acceptSpotRoutesFromChannel(SampleNames.RoomRouteChannel);
            node.addEntrySpot(BingoEntrySpot.class);
            node.addSpotFactory(BingoRoomSpot.class);
        };
    }

    @Bean
    BingoRoomDirectory bingoRoomDirectory(
        ObjectProvider<ZLinkSpotManager> spots,
        ObjectMapper json) {
        return new BingoRoomDirectory(spots.getObject(), json);
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
