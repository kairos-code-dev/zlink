package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots

import java.util.concurrent.CompletionStage
import systems.zlink.framework.actors.ZLinkBoundSession
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames

object GameNotificationPublisher {
    fun encode(result: TicTacToeGameSpot.JoinResult): String =
        encode(result.encode(), result.events)

    fun encode(result: TicTacToeGameSpot.MoveResult): String =
        encode(result.encode(), result.events)

    fun decode(value: String): PlayResponse {
        val lines = value.split("\n")
        val notifications = lines.drop(1)
            .filter { it.isNotBlank() }
            .map {
                val parts = it.split("|", limit = 3)
                Notification(parts[0], parts[1], parts[2])
            }
        return PlayResponse(lines[0], notifications)
    }

    private fun encode(reply: String, events: List<TicTacToeGameSpot.GameEvent>): String =
        buildList {
            add(reply)
            events.forEach { event ->
                add("${packetName(event)}|${event.recipientActorId}|${event.state.encode()}")
            }
        }.joinToString("\n")

    private fun packetName(event: TicTacToeGameSpot.GameEvent): String =
        when (event.kind) {
            "OpponentJoined" -> SampleNames.OpponentJoinedPacket
            "GameEnded" -> SampleNames.GameEndedPacket
            else -> SampleNames.TurnChangedPacket
        }

    fun publishAsync(session: ZLinkBoundSession, state: String): CompletionStage<Void> =
        session.send(state)
            .packetName("GameStateChanged")
            .submitAsync()

    data class PlayResponse(
        val reply: String,
        val notifications: List<Notification>,
    )

    data class Notification(
        val packetName: String,
        val recipientActorId: String,
        val payload: String,
    )
}
