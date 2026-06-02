package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play

import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameSpot
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.actors.PlayerActorFactory
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology

object PlayServer {
    fun configure(options: ZLinkFrameworkOptions) {
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
                node.addSpotFactory(TicTacToeGameSpot::class.java)
            }
            mesh.addNode(SampleNames.SessionRelayNode) { node ->
                node.enableRouter()
                node.addSpotFactory(SessionRelaySpot::class.java)
            }
        }
        options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory::class.java)
    }
}
