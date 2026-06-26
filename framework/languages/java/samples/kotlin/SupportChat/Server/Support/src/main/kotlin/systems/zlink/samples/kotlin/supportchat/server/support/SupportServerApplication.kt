package systems.zlink.samples.kotlin.supportchat.server.support

import com.fasterxml.jackson.databind.MapperFeature
import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.databind.json.JsonMapper
import org.springframework.beans.factory.ObjectProvider
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.kotlin.configureDispatch
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleNames
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors.SupportActorDirectory
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors.SupportUserActorFactory
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.conversationspot.notifications.ConversationNotificationPublisher
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.ZLinkConversationStarter
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.conversationspot.ConversationSpot
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.entryspot.SupportEntrySpot
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.conversationspot.handlers.ConversationSpotCreatedHandler
import systems.zlink.samples.kotlin.supportchat.server.support.application.assignment.AgentAssignmentService
import systems.zlink.samples.kotlin.supportchat.server.support.application.assignment.AgentAvailabilityDirectory
import systems.zlink.samples.kotlin.supportchat.server.support.application.assignment.SupportConversationAllocator



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [SupportServerApplication::class],
)
class SupportServerApplication {
    @Bean
    fun supportFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            options.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile((System.getenv("SUPPORTCHAT_LOG_DIR") ?: "logs") + "/flow-support.log")
                traceLabel("support")
            }
            options.codecs().use(ZLinkProtobufCodec.defaultCodec())
            options.addHandlersFromPackageOf(SupportServerApplication::class.java)
            options.addClientServerChannel(SampleNames.SupportChannel)
                .enableServer(SampleTopology.SupportChannelEndpoint)
                .addHandlerGroup("support")
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient()
            val route = options.addRouteMesh(SampleNames.SupportRouteChannel)
            route.enableServer(SampleTopology.SupportRouteEndpoint)
            route.enableClient()
            route.setRoutingId(RoutingId.from(SampleTopology.SupportRid))
            options.useRegistrySpotRemoteAddresses(SampleNames.SupportSpotDiscovery)
                .setRouterChannelId(SampleNames.SupportRouteChannel)
            val node = options.addSpotMesh(SampleNames.SupportSpotDiscovery)

            node.enableRouter(SampleTopology.SupportSpotRouterEndpoint)
                .setRoutingId(RoutingId.from(SampleTopology.SupportRid))
            node.enablePubSub(SampleTopology.SupportSpotEndpoint)
            node.addEntrySpot(SupportEntrySpot::class.java)
            node.addSpotFactory(ConversationSpot::class.java)
            node.addActorFactory(SampleNames.SupportActorType, SupportUserActorFactory::class.java)
        }

    @Bean
    fun supportConversationAllocator(
        spots: ObjectProvider<ZLinkSpotManager>,
        json: ObjectMapper,
    ): SupportConversationAllocator =
        SupportConversationAllocator(ZLinkConversationStarter(spots.getObject(), json))

    @Bean
    fun conversationNotificationPublisher(): ConversationNotificationPublisher =
        ConversationNotificationPublisher()

    @Bean
    fun conversationSpotCreatedHandler(json: ObjectMapper): ConversationSpotCreatedHandler =
        ConversationSpotCreatedHandler(json)

    @Bean
    fun agentAvailabilityDirectory(): AgentAvailabilityDirectory = AgentAvailabilityDirectory()

    @Bean
    fun agentAssignmentService(availability: AgentAvailabilityDirectory): AgentAssignmentService =
        AgentAssignmentService(availability)

    @Bean
    fun supportActorDirectory(): SupportActorDirectory = SupportActorDirectory()

    @Bean
    fun supportChatJsonMapper(): ObjectMapper =
        JsonMapper.builder()
            .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
            .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
            .findAndAddModules()
            .build()

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(SupportServerApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
