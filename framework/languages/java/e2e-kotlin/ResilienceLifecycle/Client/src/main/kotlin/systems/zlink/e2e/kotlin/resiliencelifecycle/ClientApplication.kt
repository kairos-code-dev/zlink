package systems.zlink.e2e.kotlin.resiliencelifecycle

import com.fasterxml.jackson.databind.ObjectMapper
import org.springframework.beans.factory.ObjectProvider
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.registry.ZLinkRegistryQueryClient
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory
import systems.zlink.framework.runtime.registry.ZLinkRemoteRegistryQueryClient
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.resiliencelifecycle.client"],
)
open class ClientApplication {
    @Bean
    open fun clientOptions(): ClientOptions =
        ClientOptions.fromEnv()

    @Bean
    open fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    open fun clientFramework(clientOptions: ClientOptions): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("${clientOptions.logDir}/client-flow.log")
                .traceLabel("kotlin-rl-client")
            options.useDiscovery().addRegistryEndpoint(clientOptions.registryRouterEndpoint)
            options.addClientServerChannel(Contracts.CHANNEL).enableClient()
        }

    @Bean
    open fun registryQueryClient(
        backendAdapterFactory: ZLinkBackendAdapterFactory,
        clientOptions: ClientOptions,
    ): ZLinkRegistryQueryClient =
        ZLinkRemoteRegistryQueryClient.connect(
            clientOptions.registryRouterEndpoint,
            backendAdapterFactory,
        )

    @Bean
    open fun clientScenario(
        client: ZLinkClient,
        registry: ObjectProvider<ZLinkRegistryQueryClient>,
        json: ObjectMapper,
        clientOptions: ClientOptions,
    ): ClientScenario =
        ClientScenario(client, registry.ifAvailable, json, clientOptions)

    companion object {
        @JvmStatic
        fun run(vararg args: String) {
            val context = SpringApplicationBuilder(ClientApplication::class.java)
                .web(WebApplicationType.NONE)
                .run(*args)
            try {
                val options = context.getBean(ClientOptions::class.java)
                context.getBean(ClientScenario::class.java).run(options.mode)
                println("resilience-lifecycle kotlin e2e result=passed")
            } finally {
                context.close()
            }
        }
    }
}
