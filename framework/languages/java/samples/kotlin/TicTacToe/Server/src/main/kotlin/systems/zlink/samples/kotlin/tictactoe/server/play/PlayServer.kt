package systems.zlink.samples.kotlin.tictactoe.server.play

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActorFactory
import systems.zlink.samples.kotlin.tictactoe.server.play.entryspot.PlayEntrySpot
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.server.play.sessions.PlaySession

object PlayServer {
    fun configure(options: ZLinkFrameworkOptions) {
        options.codecs().addJson()
        options.addHandlersFromPackageOf(PlayServer::class.java)
        options.addActorFactory(SampleNames.PlayActor, PlayActorFactory::class.java)
        options.addClientServerChannel(SampleNames.ApiChannel) { channel ->
            channel.enableClient { client ->
                client.useManualConnections { endpoints ->
                    endpoints.connect(SampleTopology.ApiEndpoint)
                }
            }
        }
        options.addClientServerChannel(SampleNames.PlayChannel) { channel ->
            channel.enableClient { client ->
                client.useManualConnections { endpoints -> endpoints.connect(SampleTopology.PlayChannelEndpoint) }
            }
            channel.enableServer { server -> server.bind(SampleTopology.PlayChannelEndpoint) }
            channel.addHandlerGroup(SampleNames.PlayChannel)
        }
        options.addSpotMesh(SampleNames.SpotMesh) { mesh ->
            mesh.addNode(SampleNames.PlayNode) { node ->
                node.enableRouter { router ->
                    router.setRoutingId(RoutingId.from(SampleNames.PlayNodeRoutingId))
                    router.setRouterBind(SampleTopology.PlaySpotRouterEndpoint)
                    router.useManualConnections { endpoints ->
                        endpoints.connect(SampleTopology.PlaySpotRouterEndpoint)
                    }
                }
                node.configureEntrySpot { entry ->
                    entry.setRoutingId(RoutingId.from(SampleNames.EntrySpotRoutingId))
                }
                node.addEntrySpot(PlayEntrySpot::class.java)
                node.addSpotFactory(TicTacToeGame::class.java)
            }
        }
        options.addStreamNode(SampleNames.PlayStream) { stream ->
            stream.bind(SampleTopology.PlayStreamEndpoint)
            stream.attachActorGateway(SampleNames.PlayNode)
            stream.registerSession(PlaySession::class.java)
        }
    }
}
