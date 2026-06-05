package systems.zlink.samples.kotlin.tictactoe.server.api

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.ApplicationContextInitializer
import org.springframework.context.ConfigurableApplicationContext
import org.springframework.context.annotation.Bean
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer
import systems.zlink.samples.kotlin.tictactoe.server.api.handlers.AuthenticatePlayerHandler
import systems.zlink.samples.kotlin.tictactoe.server.api.handlers.CreateGameHttpHandler
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [
        ApiServer::class,
        AuthenticatePlayerHandler::class,
        CreateGameHttpHandler::class,
    ],
)
class ApiServerHostFactory {
    @Bean
    fun apiOptions(settings: SampleSettings): ZLinkFrameworkOptionsCustomizer =
        ApiServer.configure(settings)

    companion object {
        fun start(settings: SampleSettings): ConfigurableApplicationContext =
            SpringApplicationBuilder(ApiServerHostFactory::class.java).also { builder ->
                builder.application().setKeepAlive(true)
            }
                .web(WebApplicationType.SERVLET)
                .properties(
                    "server.address=127.0.0.1",
                    "server.port=${settings.apiHttpPort}",
                )
                .initializers(ApplicationContextInitializer<ConfigurableApplicationContext> { context ->
                    context.beanFactory.registerSingleton("sampleSettings", settings)
                })
                .run()
    }
}
