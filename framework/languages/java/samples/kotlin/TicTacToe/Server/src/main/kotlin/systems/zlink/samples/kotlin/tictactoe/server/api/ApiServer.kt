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
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleLogging
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [
        ApiServer::class,
        AuthenticatePlayerHandler::class,
        CreateGameHttpHandler::class,
    ],
)
class ApiServer {
    @Bean
    fun apiOptions(settings: SampleSettings): ZLinkFrameworkOptionsCustomizer =
        ZLinkFrameworkOptionsCustomizer { options ->
            SampleLogging.configure(settings, "api")
            options.codecs().addJson()
            options.addHandlersFromPackageOf(ApiServer::class.java)
            options.addClientServerChannel(SampleNames.ApiChannel) { channel ->
                channel.enableServer { server -> server.bind(settings.apiChannelEndpoint) }
                channel.addHandlerGroup("api")
            }
            options.addClientServerChannel(SampleNames.PlayChannel) { channel ->
                channel.enableClient { client ->
                    client.useManualConnections { endpoints -> endpoints.connect(settings.playChannelEndpoint) }
                }
            }
        }

    companion object {
        fun start(settings: SampleSettings): ConfigurableApplicationContext =
            SpringApplicationBuilder(ApiServer::class.java).also { builder ->
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
