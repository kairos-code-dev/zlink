package systems.zlink.samples.kotlin.tictactoe.server.play.application.gamecreation

import java.util.UUID
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlayNodeInfo

class TicTacToeGameCreator(
    private val settings: SampleSettings,
) {
    fun nextRoom(gameName: String?): GameRoom {
        val normalized = gameName?.takeIf { it.isNotBlank() } ?: "tic-tac-toe"
        val roomId = "ttt-room-${UUID.randomUUID()}"
        return GameRoom(
            roomId = roomId,
            gameName = normalized,
            ownerPlayEndpoint = settings.playEndpoint,
            playEndpoints = settings.playEndpoints,
            playNodes = settings.playEndpoints.map(::PlayNodeInfo),
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
