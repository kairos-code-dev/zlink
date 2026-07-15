package systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway

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

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [CourierGatewayApplication::class],
)
class CourierGatewayApplication {
    @Bean
    fun courierGatewayFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.useCoroutineHandlers(Dispatchers.Default)
            options.addHandlersFromPackageOf(CourierGatewayApplication::class.java)
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(
                    System.getenv().getOrDefault("DELIVERYDISPATCH_LOG_DIR", "logs") +
                        "/flow-courier-gateway.log",
                )
                .traceLabel("courier-gateway")
            options.addClientServerChannel(SampleNames.CourierChannel)
                .enableServer(SampleTopology.CourierGatewayChannelEndpoint)
                .setRoutingId(RoutingId.from("delivery-courier-gateway-server"))
                .addHandlerGroup("courier-gateway")
            val courierRoutes = options.addSpotMesh(SampleNames.CourierSpotMesh)
            courierRoutes
                .enableRouter("inproc://deliverydispatch-courier-gateway-courier-client")
                .setRoutingId(RoutingId.from("deliverydispatch-courier-gateway-courier-client"))
            courierRoutes.connectRouter(
                RoutingId.from(SampleTopology.CourierActorNode1Rid),
                SampleTopology.CourierActorNode1RouterEndpoint,
            )
            courierRoutes.connectRouter(
                RoutingId.from(SampleTopology.CourierActorNode2Rid),
                SampleTopology.CourierActorNode2RouterEndpoint,
            )
        }

    @Bean
    fun courierDirectory(): CourierDirectory = CourierDirectory()

    @Bean
    fun locationStore(): ZLinkRedisLocationStore = SampleLocationStore.create()

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(CourierGatewayApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
