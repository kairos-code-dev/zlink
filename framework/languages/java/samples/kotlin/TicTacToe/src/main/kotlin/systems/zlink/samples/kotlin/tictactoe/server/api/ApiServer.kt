package systems.zlink.samples.kotlin.tictactoe.server.api

import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.samples.kotlin.tictactoe.server.api.handlers.AuthenticatePlayerHandler
import systems.zlink.samples.kotlin.tictactoe.server.api.handlers.CreateGameHttpHandler
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleTopology

object ApiServer {
    fun configure(options: ZLinkFrameworkOptions) {
        options.addClientServerChannel(SampleNames.ApiChannel) { channel ->
            channel.enableServer { server -> server.bind(SampleTopology.ApiEndpoint) }
            channel.enableClient { client ->
                client.useManualConnections { endpoints -> endpoints.connect(SampleTopology.ApiEndpoint) }
            }
            channel.addRequestHandler(
                AuthenticatePlayerHandler::class.java,
                String::class.java,
                String::class.java,
                "AuthenticatePlayer",
            )
            channel.addRequestHandler(
                CreateGameHttpHandler::class.java,
                String::class.java,
                String::class.java,
                "CreateGame",
            )
        }
    }
}
