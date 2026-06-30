package systems.zlink.e2e.kotlin.spotservice.publisher

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.spots.ZLinkSpotPublisherClient
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.spotservice.publisher"]
)
class PublisherApplication {
    @Bean
    fun publisherFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/publisher-flow.log")
                .traceLabel("kotlin-sm-publisher")
            options.addSpotMesh(Contracts.SPOT_MESH)
                .enablePubSub(Env.get("ZLINK_KOTLIN_E2E_SPOT_PUBLISHER_ENDPOINT"))
                .setRoutingId(RoutingId.from("publisher"))
        }

    companion object {
        @JvmStatic
        fun run(vararg args: String) {
            val context = SpringApplicationBuilder(PublisherApplication::class.java)
                .web(WebApplicationType.NONE)
                .run(*args)
            try {
                val publisher = context.getBean(ZLinkSpotPublisherClient::class.java)
                publisher.publishSpot(
                    Contracts.SPOT_MESH,
                    "spot.events",
                    Contracts.MeshEvent("c4-publisher")
                )
                    .packetName("MeshEvent")
                    .await()
                println("scenario SM-C4 passed")
            } finally {
                context.close()
            }
        }
    }
}
