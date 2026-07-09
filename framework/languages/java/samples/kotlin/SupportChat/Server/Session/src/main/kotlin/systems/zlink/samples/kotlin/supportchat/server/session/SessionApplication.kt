package systems.zlink.samples.kotlin.supportchat.server.session

import kotlinx.coroutines.Dispatchers
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.kotlin.configureDispatch
import systems.zlink.framework.kotlin.useCoroutineHandlers
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleFlowLog
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleLocationStore
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleNames
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.supportchat.server.session.sessions.SupportChatSession

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [SessionApplication::class],
)
class SessionApplication {
    @Bean
    fun sessionFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val topology = SampleTopology.create()
            val session = topology.primarySession
            options.addHandlersFromPackageOf(SessionApplication::class.java)
            options.useCoroutineHandlers(Dispatchers.Default)
            options.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile(SampleFlowLog.path("session"))
                traceLabel("session")
            }
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient()
            options.addClientServerChannel(SampleNames.SupportChannel)
                .enableClient()
            val node = options.addSpotMesh(SampleNames.SupportSpotDiscovery)
            node.enableRouter(session.routerEndpoint)
                .setRoutingId(session.routingId)
            node.enablePubSub(session.pubEndpoint)
            options.addStreamNode(SampleNames.StreamNode)
                .bind(session.streamEndpoint)
                .registerSession(SupportChatSession::class.java)
        }

    @Bean
    fun locationStore(): ZLinkRedisLocationStore = SampleLocationStore.create()

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(SessionApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
