package systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow

import org.springframework.boot.ApplicationArguments
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.kotlin.configureDispatch
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.shoppingmall.server.configuration.CommerceStore
import systems.zlink.samples.kotlin.shoppingmall.server.configuration.SampleLocationStore
import systems.zlink.samples.kotlin.shoppingmall.server.configuration.SampleNames
import systems.zlink.samples.kotlin.shoppingmall.server.configuration.SampleTopology

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [OrderWorkflowApplication::class],
)
class OrderWorkflowApplication {
    @Bean
    fun instanceOptions(arguments: ApplicationArguments): OrderWorkflowInstanceOptions =
        OrderWorkflowInstanceOptions.fromArgs(arguments.sourceArgs)

    @Bean
    fun commerceStore(): CommerceStore = CommerceStore()

    @Bean
    fun locationStore(): ZLinkRedisLocationStore = SampleLocationStore.create()

    @Bean
    fun orderWorkflowFramework(options: OrderWorkflowInstanceOptions): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { configurer ->
            configurer.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile((System.getenv("SHOPPINGMALL_LOG_DIR") ?: "logs") + "/flow-${options.instanceId}.log")
                traceLabel(options.instanceId)
            }
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
