package systems.zlink.samples.kotlin.deliverydispatch.server.dispatchcenter

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

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [DispatchCenterApplication::class],
)
class DispatchCenterApplication {
    @Bean
    fun dispatchWorkQueue(): DispatchWorkQueue = DispatchWorkQueue()

    @Bean
    fun dispatchCenterFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            options.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile((System.getenv("DELIVERYDISPATCH_LOG_DIR") ?: "logs") + "/flow-dispatch-center.log")
                traceLabel("dispatch-center")
            }
            options.codecs().addJson()
            options.addHandlersFromPackageOf(DispatchCenterApplication::class.java)
            options.addClientServerChannel(SampleNames.DispatchChannel)
                .enableServer(SampleTopology.DispatchChannelEndpoint)
                .addHandlerGroup("dispatch")
            options.addClientServerChannel(SampleNames.CourierAChannel)
                .enableClient()
            options.addClientServerChannel(SampleNames.CourierBChannel)
                .enableClient()
            options.addClientServerChannel(SampleNames.TrackingChannel)
                .enableClient()
        }

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(DispatchCenterApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
