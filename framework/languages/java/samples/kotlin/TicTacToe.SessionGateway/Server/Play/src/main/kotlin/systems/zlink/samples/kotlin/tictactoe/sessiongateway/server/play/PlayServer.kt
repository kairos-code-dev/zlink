package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play

import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.contracts.core.RoutingId
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.entryspot.TicTacToeEntrySpot
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.actors.PlayerActorFactory
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology

object PlayServer {
    fun configure(options: ZLinkFrameworkOptions) {
        options.addHandlersFromPackageOf(PlayServer::class.java)
        options.addClientServerChannel(SampleNames.PlayChannel) { channel ->
            channel.enableServer { server -> server.bind(SampleTopology.PlayChannelEndpoint) }
            channel.addHandlerGroup("play")
        }
        options.addRouteMeshChannel(SampleNames.PlayRouteChannel) { route ->
            route.bind(SampleTopology.PlayRouteEndpoint)
            route.configureRouting { routing ->
                routing.setRoutingId(RoutingId.from(SampleNames.PlayRid))
            }
            route.useManualConnections { endpoints ->
                endpoints.connect(SampleTopology.SessionRouteEndpoint)
                endpoints.connect(SampleTopology.ReconnectSessionRouteEndpoint)
            }
        }
        options.useRegistrySpotRemoteAddresses(SampleNames.SpotMesh) { registry ->
            registry.setRouterChannelId(SampleNames.PlayRouteChannel)
        }
        options.addSpotMesh(SampleNames.SpotMesh) { mesh ->
            mesh.addNode(SampleNames.PlayNode) { node ->
                node.enableRouter { router ->
                    router.bindRouter(SampleTopology.PlaySpotRouterEndpoint)
                    router.setRoutingId(RoutingId.from(SampleNames.PlayRid))
                }
                node.acceptSpotRoutesFromChannel(SampleNames.PlayRouteChannel)
                node.configureEntrySpot { entry ->
                    entry.setRoutingId(RoutingId.from(SampleNames.EntrySpotRoutingId))
                }
                node.addEntrySpot(TicTacToeEntrySpot::class.java)
            }
        }
        options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory::class.java)
    }

}
