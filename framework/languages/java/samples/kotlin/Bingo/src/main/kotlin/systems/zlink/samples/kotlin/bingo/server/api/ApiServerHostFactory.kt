package systems.zlink.samples.kotlin.bingo.server.api

import systems.zlink.framework.ZLinkFramework
import systems.zlink.samples.kotlin.bingo.server.api.handlers.AuthenticatePlayerHandler
import systems.zlink.samples.kotlin.bingo.server.api.handlers.MatchBingoHandler
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTopology
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticatePlayerReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticatePlayerRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiRes

object ApiServerHostFactory {
    fun start(): ZLinkFramework =
        ZLinkFramework.start { options ->
            options.useDiscovery { discovery -> discovery.add(SampleTopology.RegistryRouterEndpoint) }
            options.addClientServerChannel(SampleNames.ApiChannel) { channel ->
                channel.enableServer { server -> server.bind(SampleTopology.ApiChannelEndpoint) }
                channel.addRequestHandler(
                    AuthenticatePlayerHandler::class.java,
                    AuthenticatePlayerReq::class.java,
                    AuthenticatePlayerRes::class.java,
                    "AuthenticatePlayer",
                )
                channel.addRequestHandler(
                    MatchBingoHandler::class.java,
                    MatchBingoApiReq::class.java,
                    MatchBingoApiRes::class.java,
                    "MatchBingo",
                )
            }
            options.addClientServerChannel(SampleNames.PlayChannel) { channel -> channel.enableClient() }
        }
}
