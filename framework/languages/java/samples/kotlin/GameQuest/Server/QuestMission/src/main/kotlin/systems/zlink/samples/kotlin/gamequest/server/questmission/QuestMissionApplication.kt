package systems.zlink.samples.kotlin.gamequest.server.questmission

import com.fasterxml.jackson.databind.MapperFeature
import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.databind.json.JsonMapper
import com.fasterxml.jackson.module.kotlin.registerKotlinModule
import org.springframework.boot.ApplicationArguments
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.kotlin.configureDispatch
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.gamequest.server.configuration.QuestMissionInstanceTopology
import systems.zlink.samples.kotlin.gamequest.server.configuration.SampleNames
import systems.zlink.samples.kotlin.gamequest.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.gamequest.server.questmission.spots.PlayerQuestSpot

/**
 * Stateful QuestMission server. Subscribes to the gameplay event fanout, owns
 * the per-player `PlayerQuestSpot` aggregates and event-sourced quest state, and
 * hosts the mission action channel used for sync reconciliation. Mirrors the
 * .NET QuestMission WebApplication.
 */
@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [QuestMissionApplication::class],
)
class QuestMissionApplication {
    @Bean
    fun missionOptions(arguments: ApplicationArguments): QuestMissionOptions =
        QuestMissionOptions.fromArgs(arguments.sourceArgs)

    @Bean
    fun instanceTopology(options: QuestMissionOptions): QuestMissionInstanceTopology =
        SampleTopology.forQuestMission(options.missionName)

    @Bean
    fun gameQuestJsonMapper(): ObjectMapper =
        JsonMapper.builder()
            .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
            .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
            .build()
            .registerKotlinModule()

    @Bean
    fun questMissionFramework(instance: QuestMissionInstanceTopology): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { configurer ->
            configurer.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            configurer.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile((System.getenv("GAMEQUEST_LOG_DIR") ?: "logs") + "/flow-${instance.missionName}.log")
                traceNodeId(instance.missionName)
            }
            configurer.codecs().addJson()
            configurer.addHandlersFromPackageOf(QuestMissionApplication::class.java)

            // gameplay event fanout subscriber (both GameApi publishers)
            configurer.addFanoutChannel(SampleNames.FanoutChannel)
                .enableSubscriber()
                .enableSubscriber()
                .addHandlerGroup("gamequest-gameplay")

            // mission action channel: GameApi sync calls land here
            configurer.addClientServerChannel(SampleNames.questMissionChannel(instance.missionName))
                .enableServer(SampleTopology.missionActionEndpoint(instance.missionName))
                .addHandlerGroup("gamequest-mission")

            // outbound: notify the bound GameApi and read its gameplay snapshot
            configurer.addClientServerChannel(SampleNames.gameApiActionChannel("api-a"))
                .enableClient()
            configurer.addClientServerChannel(SampleNames.gameApiActionChannel("api-b"))
                .enableClient()

            // per-player owner spot mesh
            val node = configurer.addSpotMesh(SampleNames.QuestSpotDiscovery)
                .addNode(SampleNames.QuestSpotNode)
            node.enableRouter(instance.spotRouterEndpoint)
                .setRouterRoutingId(instance.spotRid)
            node.enablePubSub(instance.spotEndpoint)
            node.addSpotFactory(PlayerQuestSpot::class.java)
        }

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(QuestMissionApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
