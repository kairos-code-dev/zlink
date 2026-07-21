package systems.zlink.e2e.kotlin.registrymessaging.provider

import java.io.File
import org.springframework.boot.ApplicationRunner
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.registrymessaging.provider.Configuration.ServerOptions
import systems.zlink.e2e.kotlin.registrymessaging.provider.Endpoints.ProviderEndpoints
import systems.zlink.e2e.kotlin.registrymessaging.provider.Handlers.EvidenceDispatchErrorObserver
import systems.zlink.e2e.kotlin.registrymessaging.provider.Handlers.PayloadRequestHandler
import systems.zlink.e2e.kotlin.registrymessaging.provider.Handlers.ProfileCommandHandler
import systems.zlink.e2e.kotlin.registrymessaging.provider.Handlers.ProfileRequestHandler
import systems.zlink.e2e.kotlin.registrymessaging.provider.Handlers.RoutePingHandler
import systems.zlink.e2e.kotlin.registrymessaging.provider.Infrastructure.EvidenceStore
import systems.zlink.e2e.kotlin.registrymessaging.shared.Contracts
import systems.zlink.e2e.kotlin.registrymessaging.shared.PayloadRes
import systems.zlink.e2e.kotlin.registrymessaging.shared.PayloadReq
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileMsg
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileRes
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileReq
import systems.zlink.e2e.kotlin.registrymessaging.shared.ScenarioRoutePingReq
import systems.zlink.e2e.kotlin.registrymessaging.shared.ScenarioRoutePingRes
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

private lateinit var parsedOptions: ServerOptions

fun main(args: Array<String>) {
    parsedOptions = ServerOptions.parse(args)
    File(parsedOptions.logDir).mkdirs()
    val context = SpringApplicationBuilder(ProviderApplication::class.java)
        .web(WebApplicationType.NONE)
        .run(*args)
    ProviderEndpoints(parsedOptions, context).start()
}

@EnableZLinkFramework
@SpringBootApplication(proxyBeanMethods = false)
class ProviderApplication {
    @Bean
    fun serverOptions(): ServerOptions = parsedOptions

    @Bean
    fun evidenceStore(options: ServerOptions): EvidenceStore =
        EvidenceStore(options.rid, options.instanceId, options.evidenceFile)

    @Bean
    fun framework(options: ServerOptions, evidence: EvidenceStore): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { framework ->
            framework.configureDispatch()
                .setMessageFlowObserver(EvidenceDispatchErrorObserver(evidence))
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("${options.logDir}/${options.rid}-flow.log")
                .traceLabel(options.rid)

            if (!options.channelEndpoint.isNullOrBlank()) {
                val channel = framework.addClientServerChannel(Contracts.PROFILE_CHANNEL)
                channel.configureServerSocket().maxMessageSize(options.maxMessageSize)
                channel
                    .enableServer(options.channelEndpoint)
                    .enableClient()
                    .setRoutingId(RoutingId.from(options.rid))
                channel.addRequestHandler(
                    ProfileRequestHandler::class.java,
                    ProfileReq::class.java,
                    ProfileRes::class.java,
                    Contracts.PROFILE_REQUEST_PACKET,
                )
                channel.addRequestHandler(
                    PayloadRequestHandler::class.java,
                    PayloadReq::class.java,
                    PayloadRes::class.java,
                    Contracts.PAYLOAD_REQUEST_PACKET,
                )
                channel.addSendHandler(
                    ProfileCommandHandler::class.java,
                    ProfileMsg::class.java,
                    Contracts.PROFILE_COMMAND_PACKET,
                )
            }

            if (!options.manualClientEndpoint.isNullOrBlank()) {
                framework.addClientServerChannel(Contracts.PROFILE_MANUAL_CHANNEL)
                    .enableClient(options.manualClientEndpoint)
            }

            if (!options.routeEndpoint.isNullOrBlank()) {
                val route = framework.addRouteMesh(Contracts.PROFILE_ROUTE_CHANNEL)
                    .listen(options.routeEndpoint)
                    .setRoutingId(RoutingId.from(options.rid))
                route.channelName(Contracts.PROFILE_ROUTE_CHANNEL)
                for (peer in options.routePeers) {
                    route.peerConnections().connect(peer)
                }
                route.addRouteRequestHandler(
                    RoutePingHandler::class.java,
                    ScenarioRoutePingReq::class.java,
                    ScenarioRoutePingRes::class.java,
                )
            }
        }

    @Bean
    fun locationStore(options: ServerOptions): ZLinkRedisLocationStore =
        ZLinkRedisLocationStore(
            ZLinkRedisLocationOptions()
                .setConnectionString(options.redisLocationEndpoint)
                .setKeyPrefix(options.locationKeyPrefix),
        )

    @Bean
    fun applyInitialSocketWeight(
        options: ServerOptions,
        runtimeOptions: ZLinkChannelRuntimeOptions,
    ): ApplicationRunner =
        ApplicationRunner {
            val serverSocket = runtimeOptions
                .clientServerChannel(Contracts.PROFILE_CHANNEL)
                .configureServerSocket()
            serverSocket.weight(options.weight)
        }

    @Bean
    fun profileRequestHandler(evidence: EvidenceStore): ProfileRequestHandler =
        ProfileRequestHandler(evidence)

    @Bean
    fun profileCommandHandler(evidence: EvidenceStore): ProfileCommandHandler =
        ProfileCommandHandler(evidence)

    @Bean
    fun payloadRequestHandler(evidence: EvidenceStore): PayloadRequestHandler =
        PayloadRequestHandler(evidence)

    @Bean
    fun routePingHandler(evidence: EvidenceStore): RoutePingHandler =
        RoutePingHandler(evidence)
}
