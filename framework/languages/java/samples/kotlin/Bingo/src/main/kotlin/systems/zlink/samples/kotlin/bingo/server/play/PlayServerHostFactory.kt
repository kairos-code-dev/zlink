package systems.zlink.samples.kotlin.bingo.server.play

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.ZLinkFramework
import systems.zlink.samples.kotlin.bingo.server.play.actors.PlayerActorFactory
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.server.play.handlers.AllocateBingoRoomHandler
import systems.zlink.samples.kotlin.bingo.server.play.handlers.EnsurePlayerActorHandler
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTopology
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorRes

object PlayServerHostFactory {
    fun start(): ZLinkFramework =
        ZLinkFramework.start { options ->
            options.useDiscovery { discovery -> discovery.add(SampleTopology.RegistryRouterEndpoint) }
            options.addClientServerChannel(SampleNames.PlayChannel) { channel ->
                channel.enableServer { server -> server.bind(SampleTopology.PlayChannelEndpoint) }
                channel.addRequestHandler(
                    EnsurePlayerActorHandler::class.java,
                    EnsurePlayerActorReq::class.java,
                    EnsurePlayerActorRes::class.java,
                    "EnsurePlayerActor",
                )
                channel.addRequestHandler(
                    AllocateBingoRoomHandler::class.java,
                    AllocateBingoRoomReq::class.java,
                    AllocateBingoRoomRes::class.java,
                    "AllocateBingoRoom",
                )
            }
            options.addClientServerChannel(SampleNames.ApiChannel) { channel -> channel.enableClient() }
            options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory::class.java)
            options.addSpotMesh(SampleNames.RoomSpotDiscovery) { mesh ->
                mesh.addNode(SampleNames.RoomSpotNode) { node ->
                    node.enableRouter { router ->
                        router.setRouterBind(SampleTopology.PlaySpotRouterEndpoint)
                        router.setRoutingId(RoutingId.from("2202"))
                    }
                    node.enablePubSub { pubSub -> pubSub.setPubBind(SampleTopology.PlaySpotEndpoint) }
                    node.attachChannelClient(SampleNames.ApiChannel)
                    node.addSpotFactory(BingoRoomSpot::class.java)
                }
            }
        }
}
