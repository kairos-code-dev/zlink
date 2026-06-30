package systems.zlink.e2e.kotlin.discoveryregistryha.consumer

import com.fasterxml.jackson.databind.ObjectMapper
import org.springframework.boot.ApplicationArguments
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.e2e.kotlin.discoveryregistryha.Contracts
import systems.zlink.e2e.kotlin.discoveryregistryha.consumer.Configuration.ConsumerOptions
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.discoveryregistryha.consumer"],
)
class ConsumerApplication {
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

    @Bean
    fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    fun consumerOptions(args: ApplicationArguments): ConsumerOptions =
        ConsumerOptions.parse(args.sourceArgs)

    @Bean
    fun consumerFramework(consumerOptions: ConsumerOptions): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.codecs().addJson()
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("${consumerOptions.logDir}/${consumerOptions.rid}-flow.log")
                .traceLabel(consumerOptions.rid)
            for (registry in consumerOptions.registryRouters) {
                options.useDiscovery().addRegistryEndpoint(registry)
            }
            options.addClientServerChannel(Contracts.CHANNEL).enableClient()
        }

    @Bean
    fun consumerHttpServer(
        client: ZLinkClient,
        json: ObjectMapper,
        consumerOptions: ConsumerOptions,
    ): ConsumerHttpServer =
        ConsumerHttpServer(client, json, consumerOptions)
}
