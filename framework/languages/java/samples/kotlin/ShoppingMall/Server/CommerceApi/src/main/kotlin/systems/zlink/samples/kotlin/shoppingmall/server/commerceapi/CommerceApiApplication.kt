package systems.zlink.samples.kotlin.shoppingmall.server.commerceapi

import org.springframework.boot.ApplicationArguments
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.shoppingmall.server.configuration.CommerceStore
import systems.zlink.samples.kotlin.shoppingmall.server.configuration.SampleNames
import systems.zlink.samples.kotlin.shoppingmall.server.configuration.SampleTopology

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [CommerceApiApplication::class],
)
class CommerceApiApplication {
    @Bean
    fun zlinkCoroutineRuntime(): ZLinkCoroutineRuntime = ZLinkCoroutineRuntime()

    @Bean
    fun instanceOptions(arguments: ApplicationArguments): CommerceApiInstanceOptions =
        CommerceApiInstanceOptions.fromArgs(arguments.sourceArgs)

    @Bean
    fun commerceStore(): CommerceStore = CommerceStore()

    @Bean
    fun commerceApiFramework(options: CommerceApiInstanceOptions): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { configurer ->
            configurer.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            configurer.codecs().addJson()
            configurer.addHandlersFromPackageOf(CommerceApiApplication::class.java)

            configurer.addClientServerChannel(SampleNames.commerceApiChannel(options.instanceId))
                .enableServer(SampleTopology.commerceApiEndpoint(options.instanceId))
                .addHandlerGroup("commerce")

            val peer = CommerceApiInstanceOptions.peerInstance(options.instanceId)
            configurer.addClientServerChannel(SampleNames.commerceApiChannel(peer))
                .enableClient(SampleTopology.commerceApiEndpoint(peer))

            configurer.addClientServerChannel(SampleNames.workflowChannel(SampleNames.WorkflowInstanceA))
                .enableClient(SampleTopology.workflowEndpoint(SampleNames.WorkflowInstanceA))
            configurer.addClientServerChannel(SampleNames.workflowChannel(SampleNames.WorkflowInstanceB))
                .enableClient(SampleTopology.workflowEndpoint(SampleNames.WorkflowInstanceB))
        }

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(CommerceApiApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            context.getBean(CommerceStore::class.java).seedDefaults()
            return AutoCloseable { context.close() }
        }
    }
}
