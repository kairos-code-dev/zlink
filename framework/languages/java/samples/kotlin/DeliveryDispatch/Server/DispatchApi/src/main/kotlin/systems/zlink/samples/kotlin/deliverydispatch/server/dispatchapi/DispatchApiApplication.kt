package systems.zlink.samples.kotlin.deliverydispatch.server.dispatchapi

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.EvidenceStore
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTopology

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [DispatchApiApplication::class],
)
class DispatchApiApplication {
    @Bean
    fun evidenceStore(): EvidenceStore = EvidenceStore()

    @Bean
    fun dispatchApiFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            options.codecs().addJson()
            options.addHandlersFromPackageOf(DispatchApiApplication::class.java)
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableServer(SampleTopology.ApiChannelEndpoint)
                .addHandlerGroup("api")
            options.addClientServerChannel(SampleNames.DispatchChannel)
                .enableClient()
        }

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(DispatchApiApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
