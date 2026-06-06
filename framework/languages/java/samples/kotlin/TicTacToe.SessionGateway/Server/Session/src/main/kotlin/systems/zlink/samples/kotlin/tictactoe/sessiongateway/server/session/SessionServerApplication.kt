package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.ApplicationContextInitializer
import org.springframework.context.ConfigurableApplicationContext
import org.springframework.context.annotation.Bean
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.PlayerSessionDirectory
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers.PlayNotificationRelay
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleSessionNode
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [SessionServer::class],
)
class SessionServerApplication {
    @Bean
    fun zlinkCoroutineRuntime(): ZLinkCoroutineRuntime =
        ZLinkCoroutineRuntime()

    @Bean
    fun sessionFramework(sessionNode: SampleSessionNode): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.useDiscovery { discovery ->
                discovery.addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            }
            SessionServer.configureRelayNode(options, sessionNode)
            SessionServer.configure(options, sessionNode)
        }

    @Bean
    fun playerSessionDirectory(): PlayerSessionDirectory =
        PlayerSessionDirectory()

    @Bean
    fun playNotificationRelay(sessions: PlayerSessionDirectory): PlayNotificationRelay =
        PlayNotificationRelay(sessions)

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(SessionServerApplication::class.java)
                .web(WebApplicationType.NONE)
                .initializers(ApplicationContextInitializer<ConfigurableApplicationContext> { context ->
                    context.beanFactory.registerSingleton(
                        "sampleSessionNode",
                        sampleSessionNode(args),
                    )
                })
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }

        private fun sampleSessionNode(args: Array<String>): SampleSessionNode =
            if (args.any { it == "--reconnect" }) {
                SampleTopology.reconnectSession()
            } else {
                SampleTopology.primarySession()
            }
    }
}
