package systems.zlink.samples.bingo.server.play;

import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.json.JsonMapper;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer;
import systems.zlink.samples.bingo.server.play.actors.PlayerActorFactory;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoNotificationPublisher;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomSpot;
import systems.zlink.samples.bingo.server.play.bingoroomspots.handlers.BingoRoomSpotCreatedHandler;
import systems.zlink.samples.bingo.server.play.entryspot.BingoEntrySpot;
import systems.zlink.samples.bingo.server.play.handlers.BingoRoomDirectory;
import systems.zlink.samples.bingo.shared.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.configuration.SampleTopology;



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = PlayServerHostFactory.class)
public final class PlayServerHostFactory {
    private PlayServerHostFactory() {
    }

    public static AutoCloseable start(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(PlayServerHostFactory.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkOptionsCustomizer playOptions() {
        return options -> {
            options.useDiscovery(discovery ->
                discovery.addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint));
            options.codecs().addJson();
            options.addHandlersFromPackageOf(PlayServerHostFactory.class);
            options.addClientServerChannel(SampleNames.PlayChannel, channel -> {
                channel.enableServer(server -> server.bind(SampleTopology.PlayChannelEndpoint));
                channel.addHandlerGroup("play");
            });
            options.addClientServerChannel(SampleNames.ApiChannel, channel -> channel.enableClient());
            options.addRouteMeshChannel(SampleNames.RoomRouteChannel, route -> {
                route.bind(SampleTopology.PlayRouteEndpoint);
                route.configureRouting(routing ->
                    routing.setRoutingId(RoutingId.from(SampleTopology.PlayRid)));
                route.useManualConnections(endpoints ->
                    endpoints.connect(SampleTopology.SessionRouteEndpoint));
            });
            options.useRegistrySpotRemoteAddresses(SampleNames.RoomSpotDiscovery, registry ->
                registry.setRouterChannelId(SampleNames.RoomRouteChannel));
            options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory.class);
            options.addSpotMesh(SampleNames.RoomSpotDiscovery, mesh ->
                mesh.addNode(SampleNames.RoomSpotNode, node -> {
                    node.enableRouter(router -> {
                        router.bindRouter(SampleTopology.PlaySpotRouterEndpoint);
                        router.setRoutingId(RoutingId.from(SampleTopology.PlayRid));
                    });
                    node.enablePubSub(pubSub -> pubSub.bindPubSub(SampleTopology.PlaySpotEndpoint));
                    node.attachChannelClient(SampleNames.ApiChannel);
                    node.acceptSpotRoutesFromChannel(SampleNames.RoomRouteChannel);
                    node.addEntrySpot(BingoEntrySpot.class);
                    node.addSpotFactory(BingoRoomSpot.class);
                }));
        };
    }

    @Bean
    BingoRoomDirectory bingoRoomDirectory(
        ZLinkSpotManager spots,
        ObjectMapper json) {
        return new BingoRoomDirectory(spots, json);
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
