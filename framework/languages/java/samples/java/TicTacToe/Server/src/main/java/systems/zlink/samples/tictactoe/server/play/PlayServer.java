package systems.zlink.samples.tictactoe.server.play;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer;
import systems.zlink.samples.tictactoe.server.configuration.SampleLogging;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.play.actors.PlayActorFactory;
import systems.zlink.samples.tictactoe.server.play.entryspot.PlayEntrySpot;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGame;
import systems.zlink.samples.tictactoe.server.play.sessions.PlaySession;

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = PlayServer.class)
public class PlayServer {
    public static ConfigurableApplicationContext start(SampleSettings settings) {
        return new SpringApplicationBuilder(PlayServer.class)
            .web(WebApplicationType.NONE)
            .initializers(context ->
                context.getBeanFactory().registerSingleton("sampleSettings", settings))
            .run();
    }

    @Bean
    ZLinkFrameworkOptionsCustomizer playOptions(SampleSettings settings) {
        return options -> {
            SampleSettings.setCurrent(settings);
            SampleLogging.configure(settings, "play");
            options.codecs().addJson();
            options.addHandlersFromPackageOf(PlayServer.class);
            options.addActorFactory(SampleNames.PlayActor, PlayActorFactory.class);
            options.addClientServerChannel(SampleNames.ApiChannel, channel ->
                channel.enableClient(client -> client.useManualConnections(
                    endpoints -> endpoints.connect(settings.apiChannelEndpoint()))));
            options.addClientServerChannel(SampleNames.PlayChannel, channel -> {
                channel.enableClient(client -> client.useManualConnections(
                    endpoints -> endpoints.connect(settings.playChannelEndpoint())));
                channel.enableServer(server -> server.bind(settings.playChannelEndpoint()));
                channel.addHandlerGroup(SampleNames.PlayChannel);
            });
            options.addSpotMesh(SampleNames.SpotMesh, mesh ->
                mesh.addNode(SampleNames.PlayNode, node -> {
                    node.enableRouter(router -> {
                        router.setRoutingId(RoutingId.from(SampleNames.PlayNodeRoutingId));
                        router.setRouterBind(settings.playRouterEndpoint());
                        router.useManualConnections(endpoints ->
                            endpoints.connect(settings.playRouterEndpoint()));
                    });
                    node.configureEntrySpot(entry ->
                        entry.setRoutingId(RoutingId.from(SampleNames.EntrySpotRoutingId)));
                    node.addEntrySpot(PlayEntrySpot.class);
                    node.addSpotFactory(TicTacToeGame.class);
                }));
            options.addStreamNode(SampleNames.PlayStream, stream -> {
                stream.bind(settings.playEndpoint());
                stream.attachActorGateway(SampleNames.PlayNode);
                stream.registerSession(PlaySession.class);
            });
        };
    }
}
