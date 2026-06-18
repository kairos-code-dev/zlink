package systems.zlink.samples.gamequest.server.questmission;

import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.json.JsonMapper;
import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.gamequest.server.configuration.QuestMissionInstanceTopology;
import systems.zlink.samples.gamequest.server.configuration.SampleNames;
import systems.zlink.samples.gamequest.server.configuration.SampleTopology;
import systems.zlink.samples.gamequest.server.questmission.spots.PlayerQuestSpot;

/**
 * Stateful QuestMission server. Subscribes to the gameplay event fanout, owns
 * the per-player {@code PlayerQuestSpot} aggregates and event-sourced quest
 * state, and hosts the mission action channel used for sync reconciliation.
 * Mirrors the .NET QuestMission WebApplication.
 */
@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = QuestMissionApplication.class)
public final class QuestMissionApplication {
    private QuestMissionApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(QuestMissionApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    QuestMissionOptions missionOptions(ApplicationArguments arguments) {
        return QuestMissionOptions.fromArgs(arguments.getSourceArgs());
    }

    @Bean
    QuestMissionInstanceTopology instanceTopology(QuestMissionOptions options) {
        return SampleTopology.forQuestMission(options.missionName());
    }

    @Bean
    ObjectMapper gameQuestJsonMapper() {
        return JsonMapper.builder()
            .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
            .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
            .findAndAddModules()
            .build();
    }

    @Bean
    ZLinkFrameworkConfigurer questMissionFramework(QuestMissionInstanceTopology instance) {
        return configurer -> {
            configurer.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            configurer.codecs().addJson();
            configurer.addHandlersFromPackageOf(QuestMissionApplication.class);

            // gameplay event fanout subscriber (both GameApi publishers)
            configurer.addFanoutChannel(SampleNames.FanoutChannel)
                .enableSubscriber(SampleTopology.FanoutPublisherAEndpoint)
                .enableSubscriber(SampleTopology.FanoutPublisherBEndpoint)
                .addHandlerGroup("gamequest-gameplay");

            // mission action channel: GameApi sync calls land here
            configurer.addClientServerChannel(SampleNames.questMissionChannel(instance.missionName()))
                .enableServer(SampleTopology.missionActionEndpoint(instance.missionName()))
                .addHandlerGroup("gamequest-mission");

            // outbound: notify the bound GameApi and read its gameplay snapshot
            configurer.addClientServerChannel(SampleNames.gameApiActionChannel("api-a"))
                .enableClient(SampleTopology.gameApiActionEndpoint("api-a"));
            configurer.addClientServerChannel(SampleNames.gameApiActionChannel("api-b"))
                .enableClient(SampleTopology.gameApiActionEndpoint("api-b"));

            // per-player owner spot mesh
            ZLinkSpotNodeBuilder node = configurer.addSpotMesh(SampleNames.QuestSpotDiscovery)
                .addNode(SampleNames.QuestSpotNode);
            node.enableRouter(instance.spotRouterEndpoint())
                .setRouterRoutingId(instance.spotRid());
            node.enablePubSub(instance.spotEndpoint());
            node.addSpotFactory(PlayerQuestSpot.class);
        };
    }
}
