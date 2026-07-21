package systems.zlink.samples.kotlin.deliverydispatch.server.dispatch

import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.module.kotlin.KotlinModule
import kotlinx.coroutines.Dispatchers
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.spots.SpotHandleResolver
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore
import systems.zlink.framework.kotlin.useCoroutineHandlers
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleLocationStore
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTopology

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [DispatchServerApplication::class],
)
class DispatchServerApplication {
    @Bean
    fun dispatchFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.useCoroutineHandlers(Dispatchers.Default)
            options.addHandlersFromPackageOf(DispatchServerApplication::class.java)
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(
                    SampleTopology.LogDirectory +
                        "/flow-dispatch.log",
                )
                .traceLabel("dispatch")
            options.addClientServerChannel(SampleNames.CourierChannel)
                .enableClient()
            // The courier's decision comes back here as its own one-way message, so dispatch has
            // to be a channel server (common sample spec section 7.4).
            options.addClientServerChannel(SampleNames.DispatchChannel)
                .enableServer(SampleTopology.DispatchChannelEndpoint)
                .setRoutingId(RoutingId.from("delivery-dispatch-channel"))
                .addHandlerGroup(SampleNames.DispatchChannel)
            options.addClientServerChannel(SampleNames.TrackingChannel)
                .enableClient()
                .setRoutingId(RoutingId.from("delivery-dispatch-tracking-client"))
            val courierRoutes = options.addRouteMesh(SampleNames.CourierSpotMesh)
            courierRoutes
                .listen("inproc://deliverydispatch-dispatch-courier-client")
                .setRoutingId(RoutingId.from("deliverydispatch-dispatch-courier-client"))
            courierRoutes.peerConnections().connect(
                RoutingId.from(SampleTopology.CourierActorNode1Rid),
                SampleTopology.CourierActorNode1RouterEndpoint,
            )
            courierRoutes.peerConnections().connect(
                RoutingId.from(SampleTopology.CourierActorNode2Rid),
                SampleTopology.CourierActorNode2RouterEndpoint,
            )
        }

    @Bean
    fun locationStore(): ZLinkRedisLocationStore = SampleLocationStore.create()

    @Bean
    fun dispatchWorkQueue(worker: DispatchWorker): DispatchWorkQueue = DispatchWorkQueue(worker)

    @Bean
    fun deliveryOfferStore(): DeliveryOfferStore = DeliveryOfferStore()

    @Bean
    fun dispatchWorker(
        channels: ZLinkClient,
        routes: ZLinkRouteClient,
        spots: SpotHandleResolver,
        offers: DeliveryOfferStore,
    ): DispatchWorker = DispatchWorker(channels, routes, spots, offers)

    @Bean(destroyMethod = "close")
    fun offerDeadlineSweeper(
        offers: DeliveryOfferStore,
        worker: DispatchWorker,
    ): OfferDeadlineSweeper = OfferDeadlineSweeper(offers, worker)

    @Bean
    fun dispatchHttpServer(
        json: ObjectMapper,
        queue: DispatchWorkQueue,
    ): DispatchHttpServer = DispatchHttpServer(json, queue)

    @Bean
    fun objectMapper(): ObjectMapper =
        ObjectMapper().registerModule(KotlinModule.Builder().build())

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(DispatchServerApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
