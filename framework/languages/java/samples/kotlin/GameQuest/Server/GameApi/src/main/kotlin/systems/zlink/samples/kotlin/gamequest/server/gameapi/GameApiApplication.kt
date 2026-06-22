package systems.zlink.samples.kotlin.gamequest.server.gameapi

import org.springframework.boot.ApplicationArguments
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.gamequest.server.configuration.SampleNames
import systems.zlink.samples.kotlin.gamequest.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.gamequest.server.gameapi.session.GameQuestSession

/**
 * Stateless GameApi server. Hosts the gameplay action channel (one request
 * handler per action), publishes gameplay events over ZLink fanout, and exposes
 * the player-facing STREAM node for live quest notifications. Mirrors the .NET
 * GameApi WebApplication, with HTTP endpoints expressed as ZLink channels.
 */
@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [GameApiApplication::class],
)
class GameApiApplication {
    @Bean
    fun instanceOptions(arguments: ApplicationArguments): GameApiInstanceOptions =
        GameApiInstanceOptions.fromArgs(arguments.sourceArgs)

    @Bean
    fun gameApiFramework(options: GameApiInstanceOptions): ZLinkFrameworkConfigurer {
        val apiName = options.apiName
        return ZLinkFrameworkConfigurer { configurer ->
            configurer.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            configurer.codecs().addJson()
            configurer.addHandlersFromPackageOf(GameApiApplication::class.java)

            // public client-facing action channel hosted on this instance
            configurer.addClientServerChannel(SampleNames.gameApiActionChannel(apiName))
                .enableServer(SampleTopology.gameApiActionEndpoint(apiName))
                .addHandlerGroup("gameapi")

            // outbound: this GameApi calls both QuestMission instances for sync
            configurer.addClientServerChannel(SampleNames.questMissionChannel("mission-a"))
                .enableClient()
            configurer.addClientServerChannel(SampleNames.questMissionChannel("mission-b"))
                .enableClient()

            // gameplay event fanout publisher
            configurer.addFanoutChannel(SampleNames.FanoutChannel)
                .enablePublisher(SampleTopology.fanoutPublisherEndpointForApi(apiName))

            // player-facing stream node with quest session packets
            configurer.addStreamNode(SampleNames.StreamNode)
                .bind(SampleTopology.gameApiStreamEndpoint(apiName))
                .registerSession(GameQuestSession::class.java)
        }
    }

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(GameApiApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
