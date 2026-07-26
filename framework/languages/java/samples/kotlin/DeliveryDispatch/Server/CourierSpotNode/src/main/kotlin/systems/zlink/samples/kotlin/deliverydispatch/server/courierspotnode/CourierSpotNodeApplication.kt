package systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode

import kotlinx.coroutines.Dispatchers
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore
import systems.zlink.framework.kotlin.useCoroutineHandlers
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleLocationStore
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.spots.CourierEntrySpot

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [CourierSpotNodeApplication::class],
)
class CourierSpotNodeApplication {
    @Bean
    fun courierSpotNodeFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.useCoroutineHandlers(Dispatchers.Default)
            val node = SampleTopology.CourierNode
            val selected = NodeOptions.resolve(node)
            options.addHandlersFromPackageOf(CourierSpotNodeApplication::class.java)
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(
                    SampleTopology.LogDirectory +
                        "/flow-courier-$node.log",
                )
                .traceLabel("courier-$node")
            val spotNode = options.addRouteMesh(SampleNames.CourierSpotMesh)
            spotNode.listen(selected.routerEndpoint)
                .useAllocatedRoutingId(16, "delivery-courier")
            spotNode.addEntrySpot(CourierEntrySpot::class.java)
            spotNode.addActorFactory(SampleNames.CourierActorType, CourierActorFactory::class.java)
            // The courier's decision goes back to dispatch as its own one-way message, so this
            // node needs a way to speak to the dispatch channel (common sample spec section 7.4).
            options.addClientServerChannel(SampleNames.DispatchChannel)
                .enableClient()
                .setRoutingId(RoutingId.from("delivery-courier-$node-dispatch"))
        }

    @Bean
    fun actorDirectory(): ActorDirectory = ActorDirectory()

    @Bean
    fun locationStore(): ZLinkRedisLocationStore = SampleLocationStore.create()

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(CourierSpotNodeApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }

    private data class NodeOptions(
        val routerEndpoint: String,
    ) {
        companion object {
            fun resolve(node: String): NodeOptions =
                if (node == "node2") {
                    NodeOptions(
                        SampleTopology.CourierActorNode2RouterEndpoint,
                    )
                } else {
                    NodeOptions(
                        SampleTopology.CourierActorNode1RouterEndpoint,
                    )
                }
        }
    }
}
