package systems.zlink.samples.bingo.server.play;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer;
import systems.zlink.samples.bingo.server.play.actors.PlayerActorFactory;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomSpot;
import systems.zlink.samples.bingo.server.play.entryspot.BingoEntrySpot;
import systems.zlink.samples.bingo.shared.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.configuration.SampleTopology;

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
            options.useDiscovery(discovery -> discovery.add(SampleTopology.RegistryRouterEndpoint));
            options.addHandlersFromPackageOf(PlayServerHostFactory.class);
            options.addClientServerChannel(SampleNames.PlayChannel, channel -> {
                channel.enableServer(server -> server.bind(SampleTopology.PlayChannelEndpoint));
                channel.addHandlerGroup("play");
            });
            options.addClientServerChannel(SampleNames.ApiChannel, channel -> channel.enableClient());
            options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory.class);
            options.addSpotMesh(SampleNames.RoomSpotDiscovery, mesh ->
                mesh.addNode(SampleNames.RoomSpotNode, node -> {
                    node.enableRouter(router -> {
                        router.setRouterBind(SampleTopology.PlaySpotRouterEndpoint);
                        router.setRoutingId(RoutingId.from("2202"));
                    });
                    node.configureEntrySpot(entry ->
                        entry.setRoutingId(RoutingId.from("2202")));
                    node.enablePubSub(pubSub -> pubSub.setPubBind(SampleTopology.PlaySpotEndpoint));
                    node.attachChannelClient(SampleNames.ApiChannel);
                    node.addEntrySpot(BingoEntrySpot.class);
                    node.addSpotFactory(BingoRoomSpot.class);
                }));
        };
    }
}
