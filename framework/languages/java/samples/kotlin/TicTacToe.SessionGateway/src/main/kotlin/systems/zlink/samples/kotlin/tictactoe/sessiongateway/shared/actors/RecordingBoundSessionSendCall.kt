package systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.actors

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.actors.ZLinkBoundSessionSendCall

data class RecordingBoundSessionSendCall(
    private val pushes: MutableList<String>,
    private val message: String,
    private val packetName: String,
) : ZLinkBoundSessionSendCall {
    override fun metadata(key: String, value: String): ZLinkBoundSessionSendCall = this
    override fun packetName(packetName: String): ZLinkBoundSessionSendCall =
        copy(packetName = packetName)

    override fun submitAsync(): CompletionStage<Void> {
        pushes += "$packetName:$message"
        return CompletableFuture.completedFuture(null)
    }
}
