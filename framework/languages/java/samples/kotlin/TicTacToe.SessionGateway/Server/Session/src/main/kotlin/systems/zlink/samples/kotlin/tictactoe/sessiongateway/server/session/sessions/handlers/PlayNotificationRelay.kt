package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers

import kotlinx.coroutines.future.await
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.PlayerSessionDirectory
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.contracts.PlayResponseEnvelope

class PlayNotificationRelay(
    private val sessions: PlayerSessionDirectory,
) {
    suspend fun deliver(encodedResponse: String) {
        val response = PlayResponseEnvelope.decode(encodedResponse)
        response.notifications.forEach { notification ->
            sessions.find(notification.recipientActorId)
                ?.client()
                ?.send(notification.payload)
                ?.packetName(notification.packetName)
                ?.submitAsync()
                ?.await()
        }
    }

    fun reply(encodedResponse: String): String =
        PlayResponseEnvelope.decode(encodedResponse).reply
}
