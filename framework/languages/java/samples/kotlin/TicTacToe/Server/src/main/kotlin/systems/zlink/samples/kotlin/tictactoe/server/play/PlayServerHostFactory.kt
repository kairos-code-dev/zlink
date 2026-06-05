package systems.zlink.samples.kotlin.tictactoe.server.play

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.ApplicationContextInitializer
import org.springframework.context.ConfigurableApplicationContext
import org.springframework.context.annotation.Bean
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers.TicTacToeGameCreatedHandler

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [PlayServer::class],
)
class PlayServerHostFactory {
    @Bean
    fun playOptions(settings: SampleSettings): ZLinkFrameworkOptionsCustomizer =
        PlayServer.configure(settings)

    @Bean
    fun ticTacToeGameCreatedHandler(): TicTacToeGameCreatedHandler =
        TicTacToeGameCreatedHandler()

    companion object {
        fun start(settings: SampleSettings): ConfigurableApplicationContext =
            SpringApplicationBuilder(PlayServerHostFactory::class.java).also { builder ->
                builder.application().setKeepAlive(true)
            }
                .web(WebApplicationType.NONE)
                .initializers(ApplicationContextInitializer<ConfigurableApplicationContext> { context ->
                    context.beanFactory.registerSingleton("sampleSettings", settings)
                })
                .run()
    }
}
