package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.api

import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology

object ApiServer {
    fun configure(options: ZLinkFrameworkOptions) {
        options.addHandlersFromPackageOf(ApiServer::class.java)
        options.addClientServerChannel(SampleNames.ApiChannel) { channel ->
            channel.enableServer { server -> server.bind(SampleTopology.ApiEndpoint) }
            channel.enableClient { client ->
                client.useManualConnections { endpoints -> endpoints.connect(SampleTopology.ApiEndpoint) }
            }
            channel.addHandlerGroup("api")
        }
    }
}
