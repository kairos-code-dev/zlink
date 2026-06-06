package systems.zlink.samples.kotlin.tictactoe.server.play

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleLogging
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActorFactory
import systems.zlink.samples.kotlin.tictactoe.server.play.entryspot.PlayEntrySpot
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.server.play.sessions.PlaySession

object PlayServer {
    fun configure(settings: SampleSettings): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            SampleLogging.configure(settings, "play")
            options.codecs().addJson()
            options.addHandlersFromPackageOf(PlayServer::class.java)
            options.addActorFactory(SampleNames.PlayActor, PlayActorFactory::class.java)
            options.addClientServerChannel(SampleNames.ApiChannel) { channel ->
                channel.enableClient { client ->
                    client.useManualConnections { endpoints ->
                        endpoints.connect(settings.apiChannelEndpoint)
                    }
                }
            }
            options.addClientServerChannel(SampleNames.PlayChannel) { channel ->
                channel.enableServer { server -> server.bind(settings.playChannelEndpoint) }
                channel.addHandlerGroup(SampleNames.PlayChannel)
            }
            options.addRouteMeshChannel(SampleNames.PlayRouteChannel) { route ->
                route.bind(settings.playRouterEndpoint)
                route.configureRouting { routing ->
                    routing.setRoutingId(RoutingId.from(SampleNames.PlayRouterId))
                }
                route.useManualConnections { endpoints ->
                    endpoints.connect(settings.playRouterEndpoint)
                }
            }
            options.addSpotMesh(SampleNames.SpotMesh) { mesh ->
                mesh.addNode(SampleNames.PlayNode) { node ->
                    node.enableRouter { router ->
                        router.setRoutingId(RoutingId.from(SampleNames.PlayNodeRoutingId))
                        router.bindRouter(settings.spotEndpoint)
                    }
                    node.configureEntrySpot { entry ->
                        entry.setRoutingId(RoutingId.from(SampleNames.EntrySpotRoutingId))
                    }
                    node.addEntrySpot(PlayEntrySpot::class.java)
                    node.addSpotFactory(TicTacToeGame::class.java)
                }
            }
            options.addStreamNode(SampleNames.PlayStream) { stream ->
                stream.bind(settings.playEndpoint)
                stream.registerSession(PlaySession::class.java)
            }
        }
}
