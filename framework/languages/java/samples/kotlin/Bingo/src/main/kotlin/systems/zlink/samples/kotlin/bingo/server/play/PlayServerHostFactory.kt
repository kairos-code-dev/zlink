package systems.zlink.samples.kotlin.bingo.server.play

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.ZLinkFramework
import systems.zlink.samples.kotlin.bingo.server.play.actors.PlayerActorFactory
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTopology

object PlayServerHostFactory {
    fun start(): ZLinkFramework =
        ZLinkFramework.start { options ->
            options.addHandlersFromPackageOf(PlayServerHostFactory::class.java)
            options.useDiscovery { discovery -> discovery.add(SampleTopology.RegistryRouterEndpoint) }
            options.addClientServerChannel(SampleNames.PlayChannel) { channel ->
                channel.enableServer { server -> server.bind(SampleTopology.PlayChannelEndpoint) }
                channel.addHandlerGroup("play")
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
