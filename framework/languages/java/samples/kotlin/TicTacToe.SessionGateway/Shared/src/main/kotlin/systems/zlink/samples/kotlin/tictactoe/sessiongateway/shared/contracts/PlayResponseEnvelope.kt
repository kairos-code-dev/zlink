package systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.contracts

object PlayResponseEnvelope {
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
}

data class PlayResponse(
    val reply: String,
    val notifications: List<Notification>,
)

data class Notification(
    val packetName: String,
    val recipientActorId: String,
    val payload: String,
)
