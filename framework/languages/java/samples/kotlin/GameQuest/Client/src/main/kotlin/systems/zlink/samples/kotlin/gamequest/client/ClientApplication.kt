package systems.zlink.samples.kotlin.gamequest.client

import java.net.URI
import java.time.Duration
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.kotlin.ZLinkKotlinStreamConnector
import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.kotlin
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.gamequest.client.configuration.SampleNames
import systems.zlink.samples.kotlin.gamequest.client.configuration.SampleTimings
import systems.zlink.samples.kotlin.gamequest.client.configuration.SampleTopology
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions
import systems.zlink.stream.connector.ZLinkStreamDispatchMode

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [ClientApplication::class],
)
class ClientApplication {
    @Bean
    fun clientFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            options.codecs().addJson()
            options.addClientServerChannel(SampleNames.gameApiActionChannel(SampleNames.ApiA))
                .enableClient()
            options.addClientServerChannel(SampleNames.gameApiActionChannel(SampleNames.ApiB))
                .enableClient()
        }

    companion object {
        suspend fun run(args: Array<String> = emptyArray()) {
            val builder = SpringApplicationBuilder(ClientApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.run(*args).use { context ->
                val channels = context.getBean(ZLinkClient::class.java)
                val alice = createConnector(SampleTopology.GameApiAStreamEndpoint)
                val bob = createConnector(SampleTopology.GameApiBStreamEndpoint)
                try {
                    GameQuestClientScenario(channels).run(alice, bob)
                } finally {
                    alice.close().await()
                    bob.close().await()
                }
            }
            println("gamequest=completed")
        }

        private fun createConnector(endpoint: String): ZLinkKotlinStreamConnector =
            ZLinkStreamConnectorFactory.create(
                ZLinkStreamConnectorOptions(
                    URI.create(endpoint),
                    ZLinkStreamDispatchMode.AUTO,
                    SampleTimings.RequestTimeout,
                    2,
                    SampleTimings.ConnectTimeout,
                    64 * 1024,
                    false,
                    Duration.ofSeconds(1),
                    SampleTimings.RequestTimeout.plusSeconds(5),
                    true,
                    Duration.ofMillis(250),
                    Duration.ofSeconds(5),
                    2.0,
                ),
            ).kotlin()
    }
}
