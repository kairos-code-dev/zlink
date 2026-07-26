package systems.zlink.samples.kotlin.tictactoe.server.api

import kotlinx.coroutines.Dispatchers
import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.kotlin.configureDispatch
import systems.zlink.framework.kotlin.useCoroutineHandlers
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleLogging
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.api.handlers.AuthenticatePlayerHandler
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticatePlayerReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticatePlayerRes

object ApiServer {
    fun configure(settings: SampleSettings): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            SampleLogging.configure(settings, "api")
            options.useCoroutineHandlers(Dispatchers.Default)
            options.codecs().use(ZLinkMessagePackCodec.defaultCodec())
            options.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile(SampleLogging.flowLogPath(settings, "api-${settings.apiHttpPort}"))
                traceLabel("api-${settings.apiHttpPort}")
            }
            val apiChannel = options.addClientServerChannel(SampleNames.ApiChannel)
                .enableServer(settings.apiChannelEndpoint)
            apiChannel.addRequestHandler(
                AuthenticatePlayerHandler::class.java,
                AuthenticatePlayerReq::class.java,
                AuthenticatePlayerRes::class.java,
            )
            settings.playChannelEndpoints.forEach { endpoint ->
                options.addClientServerChannel(SampleNames.PlayChannel)
                    .enableClient(endpoint)
            }
        }
}
