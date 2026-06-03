package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session

import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.PlayerSession
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers.AuthenticateSessionPacketHandler
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers.CreateMatchSessionPacketHandler
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers.JoinMatchSessionPacketHandler
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers.PlaceMarkSessionPacketHandler
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology

object SessionServer {
    fun configure(options: ZLinkFrameworkOptions) {
        options.addClientServerChannel(SampleNames.ApiChannel) { channel ->
            channel.enableClient { client ->
                client.useManualConnections { endpoints ->
                    endpoints.connect(SampleTopology.ApiEndpoint)
                }
            }
        }
        options.addClientServerChannel(SampleNames.PlayChannel) { channel ->
            channel.enableClient { client ->
                client.useManualConnections { endpoints ->
                    endpoints.connect(SampleTopology.PlayEndpoint)
                }
            }
        }
        options.addStreamNode(SampleNames.GatewayStream) { stream ->
            stream.bind(SampleTopology.SessionEndpoint)
            stream.attachActorGateway(SampleNames.SessionRelayNode)
            stream.registerSession(PlayerSession::class.java)
            stream.addSessionPacketHandler(AuthenticateSessionPacketHandler::class.java)
            stream.addSessionPacketHandler(CreateMatchSessionPacketHandler::class.java)
            stream.addSessionPacketHandler(JoinMatchSessionPacketHandler::class.java)
            stream.addSessionPacketHandler(PlaceMarkSessionPacketHandler::class.java)
        }
    }
}
