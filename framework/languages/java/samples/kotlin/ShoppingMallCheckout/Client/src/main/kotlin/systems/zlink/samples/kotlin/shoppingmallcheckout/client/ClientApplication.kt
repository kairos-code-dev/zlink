package systems.zlink.samples.kotlin.shoppingmallcheckout.client

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.shoppingmallcheckout.client.configuration.SampleNames
import systems.zlink.samples.kotlin.shoppingmallcheckout.client.configuration.SampleTopology

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [ClientApplication::class],
)
class ClientApplication {
    @Bean
    fun clientFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            options.codecs().addJson()
            options.addClientServerChannel(SampleNames.commerceApiChannel(SampleNames.ApiInstanceA))
                .enableClient(SampleTopology.commerceApiEndpoint(SampleNames.ApiInstanceA))
            options.addClientServerChannel(SampleNames.commerceApiChannel(SampleNames.ApiInstanceB))
                .enableClient(SampleTopology.commerceApiEndpoint(SampleNames.ApiInstanceB))
        }

    companion object {
        suspend fun run(args: Array<String> = emptyArray()) {
            val builder = SpringApplicationBuilder(ClientApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.run(*args).use { context ->
                val channels = context.getBean(ZLinkClient::class.java)
                ShoppingMallCheckoutClientScenario(channels).run()
            }
            println("shoppingmall=completed")
        }
    }
}
