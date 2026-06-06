package systems.zlink.samples.kotlin.tictactoe.server.play

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.ApplicationContextInitializer
import org.springframework.context.ConfigurableApplicationContext
import org.springframework.context.annotation.Bean
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers.TicTacToeGameCreatedHandler



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [PlayServer::class],
)
class PlayServerApplication {
    @Bean
    fun zlinkCoroutineRuntime(): ZLinkCoroutineRuntime =
        ZLinkCoroutineRuntime()

    @Bean
    fun playFramework(settings: SampleSettings): ZLinkFrameworkConfigurer =
        PlayServer.configure(settings)

    @Bean
    fun ticTacToeGameCreatedHandler(): TicTacToeGameCreatedHandler =
        TicTacToeGameCreatedHandler()

    companion object {
        fun run(settings: SampleSettings): ConfigurableApplicationContext =
            SpringApplicationBuilder(PlayServerApplication::class.java).also { builder ->
                builder.application().setKeepAlive(true)
            }
                .web(WebApplicationType.NONE)
                .initializers(ApplicationContextInitializer<ConfigurableApplicationContext> { context ->
                    context.beanFactory.registerSingleton("sampleSettings", settings)
                })
                .run()
    }
}
