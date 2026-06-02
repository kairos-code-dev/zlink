package systems.zlink.samples.kotlin.tictactoe.server.play.sessions

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.streams.ZLinkSession
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkStreamError

class PlaySession : ZLinkSession {
    override fun context(): ZLinkSessionContext? = null
    override fun onConnectedAsync(): CompletionStage<Void> = CompletableFuture.completedFuture(null)
    override fun onDisconnectedAsync(): CompletionStage<Void> = CompletableFuture.completedFuture(null)
    override fun onErrorAsync(error: ZLinkStreamError): CompletionStage<Void> =
        CompletableFuture.completedFuture(null)
}
