package systems.zlink.samples.kotlin.deliverydispatch.server.courier

import org.springframework.boot.ApplicationArguments
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.kotlin.configureDispatch
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.deliverydispatch.server.courier.handlers.OfferDeliveryHandler
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryRes

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [CourierApplication::class],
)
class CourierApplication {
    @Bean
    fun courierOptions(arguments: ApplicationArguments): CourierOptions =
        CourierOptions.fromArgs(arguments.sourceArgs)

    @Bean
    fun courierFramework(options: CourierOptions): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { configurer ->
            configurer.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            configurer.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile((System.getenv("DELIVERYDISPATCH_LOG_DIR") ?: "logs") + "/flow-${options.courierId}.log")
                traceLabel(options.courierId)
            }
            configurer.addClientServerChannel(SampleNames.courierChannel(options.courierId))
                .enableServer(SampleTopology.courierEndpoint(options.courierId))
                .addRequestHandler(
                    OfferDeliveryHandler::class.java,
                    OfferDeliveryReq::class.java,
                    OfferDeliveryRes::class.java,
                )
        }

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(CourierApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
