package systems.zlink.samples.kotlin.tictactoe.server.api

import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleTopology

object ApiServer {
    fun configure(options: ZLinkFrameworkOptions) {
        options.codecs().addJson()
        options.addHandlersFromPackageOf(ApiServer::class.java)
        options.addClientServerChannel(SampleNames.ApiChannel) { channel ->
            channel.enableServer { server -> server.bind(SampleTopology.ApiEndpoint) }
            channel.addHandlerGroup("api")
        }
        options.addClientServerChannel(SampleNames.PlayChannel) { channel ->
            channel.enableClient { client ->
                client.useManualConnections { endpoints -> endpoints.connect(SampleTopology.PlayChannelEndpoint) }
            }
        }
    }
}
