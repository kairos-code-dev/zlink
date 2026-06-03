package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play

import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.contracts.core.RoutingId
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.entryspot.TicTacToeEntrySpot
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.entryspot.handlers.JoinMatchHandler
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameSpot
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.handlers.PlaceMarkHandler
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.handlers.CreateMatchRoomHandler
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.handlers.EnsurePlayerActorHandler
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.actors.PlayerActorFactory
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology

object PlayServer {
    fun configure(options: ZLinkFrameworkOptions) {
        options.addClientServerChannel(SampleNames.PlayChannel) { channel ->
            channel.enableServer { server -> server.bind(SampleTopology.PlayEndpoint) }
            channel.addRequestHandler(
                EnsurePlayerActorHandler::class.java,
                String::class.java,
                String::class.java,
                "EnsurePlayerActor",
            )
            channel.addRequestHandler(
                CreateMatchRoomHandler::class.java,
                String::class.java,
                String::class.java,
                "CreateMatchRoom",
            )
            channel.addRequestHandler(
                JoinMatchHandler::class.java,
                String::class.java,
                String::class.java,
                "JoinMatchReq",
            )
            channel.addRequestHandler(
                PlaceMarkHandler::class.java,
                String::class.java,
                String::class.java,
                "PlaceMarkReq",
            )
        }
        options.addRouteMeshChannel(SampleNames.PlayRouteChannel) { route ->
            route.bind(SampleTopology.PlayRouteEndpoint)
            route.useManualConnections { endpoints ->
                endpoints.connect(SampleTopology.PlayRouteEndpoint)
            }
        }
        options.useRegistrySpotRemoteAddresses(SampleNames.SpotMesh) { registry ->
            registry.setRouterChannelId(SampleNames.PlayRouteChannel)
        }
        options.addSpotMesh(SampleNames.SpotMesh) { mesh ->
            mesh.addNode(SampleNames.PlayNode) { node ->
                node.enableRouter()
                node.configureEntrySpot { entry ->
                    entry.setRoutingId(RoutingId.from(SampleNames.EntrySpotRoutingId))
                }
                node.addEntrySpot(TicTacToeEntrySpot::class.java)
                node.addSpotFactory(TicTacToeGameSpot::class.java)
            }
        }
        options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory::class.java)
    }

    fun configureSessionRelayNode(options: ZLinkFrameworkOptions) {
        options.addRouteMeshChannel(SampleNames.PlayRouteChannel) { route ->
            route.bind(SampleTopology.PlayRouteEndpoint)
            route.useManualConnections { endpoints ->
                endpoints.connect(SampleTopology.PlayRouteEndpoint)
            }
        }
        options.addSpotMesh(SampleNames.SpotMesh) { mesh ->
            mesh.addNode(SampleNames.SessionRelayNode) { node ->
                node.enableRouter()
                node.addSpotFactory(SessionRelaySpot::class.java)
            }
        }
        options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory::class.java)
    }
}
