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
@SpringBootApplication(proxyBeanMethods = false)
open class ConsumerApplication {
    @Bean
    open fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    open fun consumerFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/consumer-flow.log")
                .traceLabel("kotlin-rl-consumer")
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
            options.addClientServerChannel(Contracts.CHANNEL).enableClient()
        }

    @Bean
    open fun registryQueryClient(
        backendAdapterFactory: ZLinkBackendAdapterFactory,
    ): ZLinkRegistryQueryClient =
        ZLinkRemoteRegistryQueryClient.connect(
            Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"),
            backendAdapterFactory,
        )

    @Bean
    open fun consumerHttpServer(
        client: ZLinkClient,
        registry: ObjectProvider<ZLinkRegistryQueryClient>,
        json: ObjectMapper,
    ): ConsumerHttpServer =
        ConsumerHttpServer(
            client,
            registry.ifAvailable,
            json,
            Env.get("ZLINK_KOTLIN_E2E_CONSUMER_HTTP_ENDPOINT"),
        )

    companion object {
        @JvmStatic
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(ConsumerApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
