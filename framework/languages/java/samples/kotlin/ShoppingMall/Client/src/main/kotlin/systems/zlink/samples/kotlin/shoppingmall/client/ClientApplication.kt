package systems.zlink.samples.kotlin.shoppingmall.client

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.shoppingmall.client.configuration.SampleNames
import systems.zlink.samples.kotlin.shoppingmall.server.configuration.SampleLocationStore

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [ClientApplication::class],
)
class ClientApplication {
    @Bean
    fun locationStore(): ZLinkRedisLocationStore = SampleLocationStore.create()

    @Bean
    fun clientFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.addClientServerChannel(SampleNames.commerceApiChannel(SampleNames.ApiInstanceA))
                .enableClient()
            options.addClientServerChannel(SampleNames.commerceApiChannel(SampleNames.ApiInstanceB))
                .enableClient()
        }

    companion object {
        suspend fun run(args: Array<String> = emptyArray()) {
            val builder = SpringApplicationBuilder(ClientApplication::class.java)
                .web(WebApplicationType.NONE)
            val context = builder.run(*args)
            val channels = context.getBean(ZLinkClient::class.java)
            ShoppingMallClientScenario(channels).run()
        }
    }
}
