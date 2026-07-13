package systems.zlink.e2e.kotlin.spotservice.client

import systems.zlink.framework.kotlin.*

import org.springframework.boot.ApplicationRunner
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.ConfigurableApplicationContext
import org.springframework.context.annotation.Bean
import java.util.UUID
import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.spotservice.client"]
)
class ClientApplication {
    @Bean
    fun clientFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            val clientRid = Env.get("ZLINK_KOTLIN_E2E_CLIENT_RID", "client-route-mesh")
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/client-flow.log")
                .traceLabel("kotlin-sm-client")
            options.addRouteMeshChannel(Contracts.ROUTE_CHANNEL)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_ROUTE_ENDPOINT"))
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_ROUTE_A_ENDPOINT"))
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_ROUTE_B_ENDPOINT"))
                .setRoutingId(RoutingId.from(clientRid))
            options.addSpotMesh(Contracts.SPOT_MESH)
                .enableRouter(Env.get("ZLINK_KOTLIN_E2E_SPOT_ENDPOINT"))
                .setRoutingId(RoutingId.from(clientRid))
                .addSpotFactory(ClientDriverSpot::class.java)
        }

    @Bean
    fun locationStore(): ZLinkRedisLocationStore =
        ZLinkRedisLocationStore(
            ZLinkRedisLocationOptions()
                .setConnectionString(Env.get("ZLINK_KOTLIN_E2E_REDIS_LOCATION_ENDPOINT"))
                .setKeyPrefix(Env.get("ZLINK_KOTLIN_E2E_LOCATION_KEY_PREFIX"))
        )

    @Bean
    fun runDriver(
        spots: ZLinkSpotManager,
        context: ConfigurableApplicationContext
    ): ApplicationRunner =
        ApplicationRunner {
            val mode = Env.get("ZLINK_KOTLIN_E2E_CLIENT_MODE", "route-mesh")
            val result = ClientDriverSpot.configure(mode)
            result.whenComplete { _, error ->
                if (error != null) error.printStackTrace()
                Thread { context.close() }.start()
            }
            spots.create(
                ClientDriverSpot::class.java,
                RoutingId.from("client-driver-$mode-${UUID.randomUUID().toString().replace("-", "")}"),
            ).whenComplete { _, error ->
                if (error != null) result.completeExceptionally(error)
            }
        }

    companion object {
        @JvmStatic
        fun run(vararg args: String) {
            val builder = SpringApplicationBuilder(ClientApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.run(*args)
        }
    }
}
