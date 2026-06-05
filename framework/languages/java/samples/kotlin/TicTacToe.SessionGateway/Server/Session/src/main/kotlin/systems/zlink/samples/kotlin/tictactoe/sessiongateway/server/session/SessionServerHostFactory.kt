package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.PlayerSessionDirectory
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers.PlayNotificationRelay
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [SessionServer::class],
)
class SessionServerHostFactory {
    @Bean
    fun sessionOptions(): ZLinkFrameworkOptionsCustomizer =
        ZLinkFrameworkOptionsCustomizer { options ->
            options.useDiscovery { registry ->
                registry.add(SampleTopology.RegistryRouterEndpoint)
            }
            SessionServer.configureRelayNode(options)
            SessionServer.configure(options)
        }

    @Bean
    fun playerSessionDirectory(): PlayerSessionDirectory =
        PlayerSessionDirectory()

    @Bean
    fun playNotificationRelay(sessions: PlayerSessionDirectory): PlayNotificationRelay =
        PlayNotificationRelay(sessions)

    companion object {
        fun start(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(SessionServerHostFactory::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
