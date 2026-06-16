package systems.zlink.samples.kotlin.shoppingmallcheckout.server.orderworkflow

import org.springframework.boot.ApplicationArguments
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.shoppingmallcheckout.server.configuration.CommerceStore
import systems.zlink.samples.kotlin.shoppingmallcheckout.server.configuration.SampleNames
import systems.zlink.samples.kotlin.shoppingmallcheckout.server.configuration.SampleTopology

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [OrderWorkflowApplication::class],
)
class OrderWorkflowApplication {
    @Bean
    fun zlinkCoroutineRuntime(): ZLinkCoroutineRuntime = ZLinkCoroutineRuntime()

    @Bean
    fun instanceOptions(arguments: ApplicationArguments): OrderWorkflowInstanceOptions =
        OrderWorkflowInstanceOptions.fromArgs(arguments.sourceArgs)

    @Bean
    fun commerceStore(): CommerceStore = CommerceStore()

    @Bean
    fun orderWorkflowFramework(options: OrderWorkflowInstanceOptions): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { configurer ->
            configurer.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            configurer.codecs().addJson()
            configurer.addHandlersFromPackageOf(OrderWorkflowApplication::class.java)
            configurer.addClientServerChannel(SampleNames.workflowChannel(options.instanceId))
                .enableServer(SampleTopology.workflowEndpoint(options.instanceId))
                .addHandlerGroup("workflow")
        }

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(OrderWorkflowApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
