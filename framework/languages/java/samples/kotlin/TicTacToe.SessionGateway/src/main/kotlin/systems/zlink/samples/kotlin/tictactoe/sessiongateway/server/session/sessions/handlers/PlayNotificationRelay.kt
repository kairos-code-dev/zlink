package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.GameNotificationPublisher
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.PlayerSessionDirectory

object PlayNotificationRelay {
    fun deliverAsync(encodedResponse: String): CompletionStage<Void> {
        val response = GameNotificationPublisher.decode(encodedResponse)
        var tail: CompletionStage<Void> = CompletableFuture.completedFuture(null)
        response.notifications.forEach { notification ->
            tail = tail.thenCompose {
                PlayerSessionDirectory.find(notification.recipientActorId)
                    ?.client()
                    ?.send(notification.payload)
                    ?.packetName(notification.packetName)
                    ?.submitAsync()
                    ?: CompletableFuture.completedFuture(null)
            }
        }
        return tail
    }

    fun reply(encodedResponse: String): String =
        GameNotificationPublisher.decode(encodedResponse).reply
}
