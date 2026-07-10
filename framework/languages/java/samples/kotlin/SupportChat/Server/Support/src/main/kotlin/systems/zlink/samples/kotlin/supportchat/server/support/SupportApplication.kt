package systems.zlink.samples.kotlin.supportchat.server.support

import kotlinx.coroutines.Dispatchers
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.kotlin.configureDispatch
import systems.zlink.framework.kotlin.useCoroutineHandlers
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleFlowLog
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleLocationStore
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleNames
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.supportchat.server.support.application.AgentAssignmentService
import systems.zlink.samples.kotlin.supportchat.server.support.application.AgentAvailabilityDirectory
import systems.zlink.samples.kotlin.supportchat.server.support.application.ConversationStarter
import systems.zlink.samples.kotlin.supportchat.server.support.application.SupportConversationAllocator
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.FrameworkConversationStarter
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors.SupportActorDirectory
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors.SupportUserActorFactory
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors.SupportUserActorTransferAdapter
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.conversationspot.ConversationSpot
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.conversationspot.notifications.ConversationNotificationPublisher
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.entryspot.SupportEntrySpot

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [SupportApplication::class],
)
class SupportApplication {
    @Bean
    fun supportFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val topology = SampleTopology.create()
            options.addHandlersFromPackageOf(SupportApplication::class.java)
            options.useCoroutineHandlers(Dispatchers.Default)
            options.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile(SampleFlowLog.path("support"))
                traceLabel("support")
            }
            options.addClientServerChannel(SampleNames.SupportChannel)
                .enableServer(topology.supportChannelEndpoint)
                .addHandlerGroup(SampleNames.SupportChannel)
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient()
            val node = options.addSpotMesh(SampleNames.SupportSpotDiscovery)
            node.enableRouter(topology.supportEntrySpotRouterEndpoint)
                .setRoutingId(topology.supportEntryRid)
            node.enablePubSub(topology.supportEntrySpotEndpoint)
            node.addEntrySpot(SupportEntrySpot::class.java)
            node.addActorFactory(SampleNames.SupportActorType, SupportUserActorFactory::class.java)
            node.addActorTransferAdapter(
                SampleNames.SupportActorType,
                SupportUserActorTransferAdapter::class.java,
            )
            node.addSpotFactory(ConversationSpot::class.java)
        }

    @Bean
    fun locationStore(): ZLinkRedisLocationStore = SampleLocationStore.create()

    @Bean
    fun supportActorDirectory(): SupportActorDirectory = SupportActorDirectory()

    @Bean
    fun agentAvailabilityDirectory(): AgentAvailabilityDirectory =
        AgentAvailabilityDirectory(SampleNames.AgentCapacity)

    @Bean
    fun agentAssignmentService(availability: AgentAvailabilityDirectory): AgentAssignmentService =
        AgentAssignmentService(availability)

    @Bean
    fun conversationStarter(spots: ZLinkSpotManager): ConversationStarter =
        FrameworkConversationStarter(spots)

    @Bean
    fun supportConversationAllocator(conversations: ConversationStarter): SupportConversationAllocator =
        SupportConversationAllocator(conversations)

    @Bean
    fun conversationNotificationPublisher(): ConversationNotificationPublisher =
        ConversationNotificationPublisher()

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(SupportApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
