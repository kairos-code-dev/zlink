package systems.zlink.samples.kotlin.tictactoe.server.play

import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.server.play.sessions.PlaySession

object PlayServer {
    fun configure(options: ZLinkFrameworkOptions) {
        options.addSpotMesh(SampleNames.SpotMesh) { mesh ->
            mesh.addNode(SampleNames.PlayNode) { node -> node.addSpotFactory(TicTacToeGame::class.java) }
        }
        options.addStreamNode(SampleNames.PlayStream) { stream ->
            stream.bind(SampleTopology.PlayStreamEndpoint)
            stream.registerSession(PlaySession::class.java)
        }
    }
}
