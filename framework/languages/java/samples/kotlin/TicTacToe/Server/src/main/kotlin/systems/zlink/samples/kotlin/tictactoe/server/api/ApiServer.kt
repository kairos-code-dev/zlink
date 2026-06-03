package systems.zlink.samples.kotlin.tictactoe.server.api

import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleLogging
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings

object ApiServer {
    fun configure(options: ZLinkFrameworkOptions) {
        configure(options, SampleSettings.current())
    }

    fun configure(options: ZLinkFrameworkOptions, settings: SampleSettings) {
        SampleLogging.configure(settings, "api")
        options.codecs().addJson()
        options.addHandlersFromPackageOf(ApiServer::class.java)
        options.addClientServerChannel(SampleNames.ApiChannel) { channel ->
            channel.enableServer { server -> server.bind(settings.apiChannelEndpoint) }
            channel.addHandlerGroup("api")
        }
        options.addClientServerChannel(SampleNames.PlayChannel) { channel ->
            channel.enableClient { client ->
                client.useManualConnections { endpoints -> endpoints.connect(settings.playChannelEndpoint) }
            }
        }
    }
}
