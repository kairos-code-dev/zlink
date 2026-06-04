package systems.zlink.samples.kotlin.bingo.server.session

import systems.zlink.framework.ZLinkFramework
import systems.zlink.samples.kotlin.bingo.server.session.sessions.BingoSession
import systems.zlink.samples.kotlin.bingo.server.session.sessions.handlers.AuthenticateSessionHandler
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTopology

object SessionServerHostFactory {
    fun start(): ZLinkFramework =
        ZLinkFramework.start { options ->
            options.useDiscovery { discovery -> discovery.add(SampleTopology.RegistryRouterEndpoint) }
            options.addClientServerChannel(SampleNames.ApiChannel) { channel -> channel.enableClient() }
            options.addClientServerChannel(SampleNames.PlayChannel) { channel -> channel.enableClient() }
            options.addStreamNode(SampleNames.StreamNode) { stream ->
                stream.bind(SampleTopology.StreamEndpoint)
                stream.registerSession(BingoSession::class.java)
                stream.addSessionPacketHandler(AuthenticateSessionHandler::class.java)
            }
        }
}
