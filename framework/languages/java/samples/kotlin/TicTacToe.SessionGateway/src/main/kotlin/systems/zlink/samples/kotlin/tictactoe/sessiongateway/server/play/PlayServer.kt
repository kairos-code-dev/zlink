package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play

import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameSpot
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.actors.PlayerActorFactory
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames

object PlayServer {
    fun configure(options: ZLinkFrameworkOptions) {
        options.useRegistrySpotRemoteAddresses(SampleNames.SpotMesh)
        options.addSpotMesh(SampleNames.SpotMesh) { mesh ->
            mesh.addNode(SampleNames.PlayNode) { node -> node.addSpotFactory(TicTacToeGameSpot::class.java) }
            mesh.addNode(SampleNames.SessionRelayNode) { node -> node.addSpotFactory(SessionRelaySpot::class.java) }
        }
        options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory::class.java)
    }
}
