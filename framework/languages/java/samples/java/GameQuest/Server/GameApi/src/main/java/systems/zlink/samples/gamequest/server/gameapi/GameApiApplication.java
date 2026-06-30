package systems.zlink.samples.gamequest.server.gameapi;

import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.gamequest.server.configuration.SampleNames;
import systems.zlink.samples.gamequest.server.configuration.SampleTopology;
import systems.zlink.samples.gamequest.server.gameapi.session.GameQuestSession;

/**
 * Stateless GameApi server. Hosts the gameplay action channel (one request
 * handler per action), publishes gameplay events over ZLink fanout, and exposes
 * the player-facing STREAM node for live quest notifications. Mirrors the .NET
 * GameApi WebApplication, with HTTP endpoints expressed as ZLink channels.
 */
@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = GameApiApplication.class)
public final class GameApiApplication {
    private GameApiApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(GameApiApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    GameApiInstanceOptions instanceOptions(ApplicationArguments arguments) {
        return GameApiInstanceOptions.fromArgs(arguments.getSourceArgs());
    }

    @Bean
    ZLinkFrameworkConfigurer gameApiFramework(GameApiInstanceOptions options) {
        String apiName = options.apiName();
        return configurer -> {
            configurer.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            configurer.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(System.getenv().getOrDefault("GAMEQUEST_LOG_DIR", "logs") + "/flow-" + apiName + ".log")
                .traceLabel(apiName);
            configurer.addHandlersFromPackageOf(GameApiApplication.class);

            // public client-facing action channel hosted on this instance
            configurer.addClientServerChannel(SampleNames.gameApiActionChannel(apiName))
                .enableServer(SampleTopology.gameApiActionEndpoint(apiName))
                .addHandlerGroup("gameapi");

            // outbound: this GameApi calls both QuestMission instances for sync
            configurer.addClientServerChannel(SampleNames.questMissionChannel("mission-a"))
                .enableClient();
            configurer.addClientServerChannel(SampleNames.questMissionChannel("mission-b"))
                .enableClient();

            // gameplay event fanout publisher
            configurer.addFanoutChannel(SampleNames.FanoutChannel)
                .enablePublisher(SampleTopology.fanoutPublisherEndpointForApi(apiName));

            // player-facing stream node with quest session packets
            configurer.addStreamNode(SampleNames.StreamNode)
                .bind(SampleTopology.gameApiStreamEndpoint(apiName))
                .registerSession(GameQuestSession.class);
        };
    }
}
