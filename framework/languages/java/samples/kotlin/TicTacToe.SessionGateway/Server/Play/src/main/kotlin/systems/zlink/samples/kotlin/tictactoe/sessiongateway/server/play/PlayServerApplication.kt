package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology



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
    fun playFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.useDiscovery { discovery ->
                discovery.addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            }
            PlayServer.configure(options)
        }

    @Bean
    fun ticTacToeGameDirectory(): TicTacToeGameDirectory =
        TicTacToeGameDirectory()

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(PlayServerApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
