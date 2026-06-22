package systems.zlink.samples.kotlin.deliverydispatch.server.tracking

import com.fasterxml.jackson.databind.MapperFeature
import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.databind.json.JsonMapper
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.EvidenceStore
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.deliverydispatch.server.tracking.actors.CustomerActorFactory
import systems.zlink.samples.kotlin.deliverydispatch.server.tracking.spots.CustomerEntrySpot
import systems.zlink.samples.kotlin.deliverydispatch.server.tracking.spots.DeliverySpotDirectory
import systems.zlink.samples.kotlin.deliverydispatch.server.tracking.spots.DeliveryTrackingSpot

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [TrackingApplication::class],
)
class TrackingApplication {
    @Bean
    fun evidenceStore(): EvidenceStore = EvidenceStore()

    @Bean
    fun deliverySpotDirectory(): DeliverySpotDirectory = DeliverySpotDirectory()

    @Bean
    fun deliveryDispatchJsonMapper(): ObjectMapper =
        JsonMapper.builder()
            .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
            .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
            .findAndAddModules()
            .build()

    @Bean
    fun trackingFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            options.codecs().addJson()
            options.addHandlersFromPackageOf(TrackingApplication::class.java)
            options.addActorFactory(SampleNames.CustomerActorType, CustomerActorFactory::class.java)
            options.addClientServerChannel(SampleNames.TrackingChannel)
                .enableServer(SampleTopology.TrackingChannelEndpoint)
                .addHandlerGroup("tracking")
            options.addFanoutChannel(SampleNames.StatusFanoutChannel)
                .enablePublisher(SampleTopology.StatusFanoutEndpoint)
            val route = options.addRouteMeshChannel(SampleNames.SpotRouteChannel)
            route.enableServer(SampleTopology.TrackingSpotRouteEndpoint)
            route.enableClient()
            route.setRoutingId(RoutingId.from(SampleTopology.TrackingSpotNodeRid))
            options.useRegistrySpotRemoteAddresses(SampleNames.DeliverySpotDiscovery)
                .setRouterChannelId(SampleNames.SpotRouteChannel)
            val node = options.addSpotMesh(SampleNames.DeliverySpotDiscovery)
                .addNode(SampleNames.TrackingSpotNode)
            node.enableRouter(SampleTopology.TrackingSpotRouterEndpoint)
                .setRouterRoutingId(RoutingId.from(SampleTopology.TrackingSpotNodeRid))
            node.enablePubSub(SampleTopology.TrackingSpotEndpoint)
            node.acceptSpotRoutesFromChannel(SampleNames.SpotRouteChannel)
            node.addEntrySpot(CustomerEntrySpot::class.java)
            node.addSpotFactory(DeliveryTrackingSpot::class.java)
        }

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(TrackingApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
