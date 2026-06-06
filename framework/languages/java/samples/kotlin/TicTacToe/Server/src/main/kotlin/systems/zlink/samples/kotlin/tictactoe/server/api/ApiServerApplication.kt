package systems.zlink.samples.kotlin.tictactoe.server.api

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.ApplicationContextInitializer
import org.springframework.context.ConfigurableApplicationContext
import org.springframework.context.annotation.Bean
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.tictactoe.server.api.handlers.AuthenticatePlayerHandler
import systems.zlink.samples.kotlin.tictactoe.server.api.handlers.CreateGameHttpHandler
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [
        ApiServer::class,
        AuthenticatePlayerHandler::class,
        CreateGameHttpHandler::class,
    ],
)
class ApiServerApplication {
    @Bean
    fun apiFramework(settings: SampleSettings): ZLinkFrameworkConfigurer =
        ApiServer.configure(settings)

    companion object {
        fun run(settings: SampleSettings): ConfigurableApplicationContext =
            SpringApplicationBuilder(ApiServerApplication::class.java).also { builder ->
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
