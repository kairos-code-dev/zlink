package systems.zlink.e2e.kotlin.pubsub.publisher

import com.fasterxml.jackson.databind.ObjectMapper
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.e2e.kotlin.pubsub.shared.Contracts
import systems.zlink.framework.channels.ZLinkFanoutClient
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

private lateinit var parsedOptions: PublisherOptions

fun main(args: Array<String>) {
    parsedOptions = PublisherOptions.parse(args)
    val builder = SpringApplicationBuilder(PublisherApplication::class.java)
        .web(WebApplicationType.NONE)
    builder.application().setKeepAlive(true)
    builder.run(*args)
}

@EnableZLinkFramework
@SpringBootApplication(proxyBeanMethods = false)
class PublisherApplication {
    @Bean
    fun publisherOptions(): PublisherOptions = parsedOptions

    @Bean
    fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    fun publisherEndpoints(
        options: PublisherOptions,
        fanout: ZLinkFanoutClient,
        json: ObjectMapper,
    ): PublisherEndpoints =
        PublisherEndpoints(fanout, json, options.httpEndpoint)

    @Bean
    fun publisherFramework(options: PublisherOptions): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("${parsedOptions.logDir}/publisher-flow.log")
                .traceLabel("kotlin-ps-publisher")
            options.useDiscovery().addRegistryEndpoint(parsedOptions.registryRouterEndpoint)
            options.addFanoutChannel(Contracts.EVENT_CHANNEL)
                .enablePublisher(parsedOptions.publisherEndpoint)
        }
}
