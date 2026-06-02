package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session

import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.PlayerSession
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology

object SessionServer {
    fun configure(options: ZLinkFrameworkOptions) {
        options.addStreamNode(SampleNames.GatewayStream) { stream ->
            stream.bind(SampleTopology.SessionEndpoint)
            stream.attachActorGateway(SampleNames.SessionRelayNode)
            stream.registerSession(PlayerSession::class.java)
        }
    }
}
