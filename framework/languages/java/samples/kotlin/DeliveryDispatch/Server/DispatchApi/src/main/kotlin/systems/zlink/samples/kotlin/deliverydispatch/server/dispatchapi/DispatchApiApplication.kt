package systems.zlink.samples.kotlin.deliverydispatch.server.dispatchapi

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.kotlin.configureDispatch
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.EvidenceStore
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.deliverydispatch.server.dispatchapi.handlers.CreateDeliveryHandler
import systems.zlink.samples.kotlin.deliverydispatch.server.dispatchapi.handlers.ServerAssertionHandler

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [
        DispatchApiApplication::class,
        CreateDeliveryHandler::class,
        ServerAssertionHandler::class,
    ],
)
class DispatchApiApplication {
    @Bean
    fun evidenceStore(): EvidenceStore = EvidenceStore()

    @Bean
    fun dispatchApiFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            options.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile((System.getenv("DELIVERYDISPATCH_LOG_DIR") ?: "logs") + "/flow-dispatch-api.log")
                traceLabel("dispatch-api")
            }
            options.addClientServerChannel(SampleNames.DispatchChannel)
                .enableClient()
        }

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(DispatchApiApplication::class.java)
                .web(WebApplicationType.SERVLET)
                .properties(
                    "server.address=127.0.0.1",
                    "server.port=${java.net.URI.create(SampleTopology.ApiHttpUrl).port}",
                )
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
