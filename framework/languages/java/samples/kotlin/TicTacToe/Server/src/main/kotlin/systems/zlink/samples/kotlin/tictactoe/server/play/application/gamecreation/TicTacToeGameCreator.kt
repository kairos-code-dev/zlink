package systems.zlink.samples.kotlin.tictactoe.server.play.application.gamecreation

import java.util.concurrent.atomic.AtomicInteger
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlayNodeInfo

class TicTacToeGameCreator(
    private val settings: SampleSettings,
) {
    private val sequence = AtomicInteger()

    fun nextRoom(gameName: String?): GameRoom {
        val normalized = gameName?.takeIf { it.isNotBlank() } ?: "tic-tac-toe"
        val index = Math.floorMod(sequence.getAndIncrement(), settings.playEndpoints.size)
        val roomId = "ttt-room-%03d".format(sequence.get())
        val ownerPlayEndpoint = settings.playEndpoints[index]
        return GameRoom(
            roomId = roomId,
            gameName = normalized,
            ownerPlayEndpoint = ownerPlayEndpoint,
            playEndpoints = settings.playEndpoints,
            playNodes = settings.playEndpoints.mapIndexed { playIndex, endpoint ->
                PlayNodeInfo(endpoint, "play-node-${playIndex + 1}")
            },
            requiredLevel = SampleNames.RequiredLevel,
        )
    }

    data class GameRoom(
        val roomId: String,
        val gameName: String,
        val ownerPlayEndpoint: String,
        val playEndpoints: List<String>,
        val playNodes: List<PlayNodeInfo>,
        val requiredLevel: Int,
    )

}
