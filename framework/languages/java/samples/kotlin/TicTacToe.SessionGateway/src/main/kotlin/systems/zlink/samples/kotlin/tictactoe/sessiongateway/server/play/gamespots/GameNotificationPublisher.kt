package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots

import java.util.concurrent.CompletionStage
import systems.zlink.framework.actors.ZLinkBoundSession

class GameNotificationPublisher {
    fun publishAsync(session: ZLinkBoundSession, state: String): CompletionStage<Void> =
        session.send(state)
            .packetName("GameStateChanged")
            .submitAsync()
}
