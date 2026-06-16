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
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleNames
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.actors.SupportActorDirectory
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.actors.SupportUserActorFactory
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.notifications.ConversationNotificationPublisher
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.ConversationSpot
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.SupportEntrySpot
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.handlers.ConversationSpotCreatedHandler
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
    fun zlinkCoroutineRuntime(): ZLinkCoroutineRuntime =
        ZLinkCoroutineRuntime()

    @Bean
    fun supportFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            options.codecs().addProtobuf()
            options.addHandlersFromPackageOf(SupportServerApplication::class.java)
            options.addClientServerChannel(SampleNames.SupportChannel)
                .enableServer(SampleTopology.SupportChannelEndpoint)
                .addHandlerGroup("support")
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient()
            val route = options.addRouteMeshChannel(SampleNames.SupportRouteChannel)
            route.enableServer(SampleTopology.SupportRouteEndpoint)
            route.enableClient(SampleTopology.SessionRouteEndpoint)
            route.configureRouting().setRoutingId(RoutingId.from(SampleTopology.SupportRid))
            options.useRegistrySpotRemoteAddresses(SampleNames.SupportSpotDiscovery)
                .setRouterChannelId(SampleNames.SupportRouteChannel)
            options.addActorFactory(SampleNames.SupportActorType, SupportUserActorFactory::class.java)
            val node = options.addSpotMesh(SampleNames.SupportSpotDiscovery)
                .addNode(SampleNames.SupportEntrySpotNode)
            node.enableRouter(SampleTopology.SupportSpotRouterEndpoint)
                .setRouterRoutingId(RoutingId.from(SampleTopology.SupportRid))
            node.enablePubSub(SampleTopology.SupportSpotEndpoint)
            node.attachChannelClient(SampleNames.ApiChannel)
            node.acceptSpotRoutesFromChannel(SampleNames.SupportRouteChannel)
            node.addEntrySpot(SupportEntrySpot::class.java)
            node.addSpotFactory(ConversationSpot::class.java)
        }

    @Bean
    fun supportConversationAllocator(
        spots: ObjectProvider<ZLinkSpotManager>,
        json: ObjectMapper,
    ): SupportConversationAllocator =
        SupportConversationAllocator(spots.getObject(), json)

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
