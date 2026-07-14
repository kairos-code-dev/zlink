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
            options.addHandlersFromPackageOf(ApiServer::class.java)
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableServer(settings.apiChannelEndpoint)
                .addHandlerGroup("api")
            settings.playChannelEndpoints.forEachIndexed { index, endpoint ->
                options.addClientServerChannel(SampleNames.playChannel(index))
                    .enableClient(endpoint)
            }
        }
}
