package systems.zlink.samples.kotlin.tictactoe.sessiongateway.client

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CopyOnWriteArrayList
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames
import systems.zlink.stream.connector.ZLinkStreamConnector

class SessionActorNotificationInbox {
    private val turnChanged = CopyOnWriteArrayList<String>()
    private val opponentJoined = CopyOnWriteArrayList<String>()
    private val gameEnded = CopyOnWriteArrayList<String>()

    fun register(connector: ZLinkStreamConnector) {
        connector.on(SampleNames.TurnChangedPacket) { message ->
            turnChanged += message.payload().payload().toUtf8String()
            CompletableFuture.completedFuture(null)
        }
        connector.on(SampleNames.OpponentJoinedPacket) { message ->
            opponentJoined += message.payload().payload().toUtf8String()
            CompletableFuture.completedFuture(null)
        }
        connector.on(SampleNames.GameEndedPacket) { message ->
            gameEnded += message.payload().payload().toUtf8String()
            CompletableFuture.completedFuture(null)
        }
    }

    fun turnChanged(): List<String> = turnChanged.toList()
    fun opponentJoined(): List<String> = opponentJoined.toList()
    fun gameEnded(): List<String> = gameEnded.toList()
}
