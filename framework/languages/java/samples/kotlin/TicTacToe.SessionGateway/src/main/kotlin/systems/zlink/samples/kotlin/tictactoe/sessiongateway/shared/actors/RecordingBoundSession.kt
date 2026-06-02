package systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.actors

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.actors.ZLinkBoundSession
import systems.zlink.framework.actors.ZLinkBoundSessionSendCall

data class RecordingBoundSession(
    private val pushes: MutableList<String>,
) : ZLinkBoundSession {
    override fun <TMessage> send(message: TMessage): ZLinkBoundSessionSendCall =
        RecordingBoundSessionSendCall(pushes, message.toString(), message.toString())

    override fun disconnectAsync(): CompletionStage<Void> =
        CompletableFuture.completedFuture(null)
}
