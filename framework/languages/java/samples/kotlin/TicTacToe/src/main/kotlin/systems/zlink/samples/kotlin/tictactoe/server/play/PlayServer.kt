package systems.zlink.samples.kotlin.tictactoe.server.play

import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.server.play.sessions.PlaySession

object PlayServer {
    fun configure(options: ZLinkFrameworkOptions) {
        options.addHandlersFromPackageOf(PlayServer::class.java)
        options.addClientServerChannel(SampleNames.PlayChannel) { channel ->
            channel.enableServer { server -> server.bind(SampleTopology.PlayChannelEndpoint) }
            channel.addHandlerGroup("play")
        }
        options.addSpotMesh(SampleNames.SpotMesh) { mesh ->
            mesh.addNode(SampleNames.PlayNode) { node -> node.addSpotFactory(TicTacToeGame::class.java) }
        }
        options.addStreamNode(SampleNames.PlayStream) { stream ->
            stream.bind(SampleTopology.PlayStreamEndpoint)
            stream.registerSession(PlaySession::class.java)
        }
    }
}
