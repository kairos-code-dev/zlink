package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.streams.ZLinkSession
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkStreamError

class PlayerSession(
    private val actors: RecordingSessionActors = RecordingSessionActors(),
) : ZLinkSession {
    override fun context(): ZLinkSessionContext = RecordingSessionContext(actors)
    override fun onConnectedAsync(): CompletionStage<Void> = CompletableFuture.completedFuture(null)
    override fun onDisconnectedAsync(): CompletionStage<Void> = CompletableFuture.completedFuture(null)
    override fun onErrorAsync(error: ZLinkStreamError): CompletionStage<Void> =
        CompletableFuture.completedFuture(null)
}
