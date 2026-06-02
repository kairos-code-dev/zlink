package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions

import java.util.Optional
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.streams.ZLinkSessionClient
import systems.zlink.framework.streams.ZLinkSessionContext

data class RecordingSessionContext(
    private val actors: RecordingSessionActors,
) : ZLinkSessionContext {
    override fun sessionId(): String = "session-1"
    override fun routingId(): Optional<RoutingId> = Optional.of(RoutingId.from("session-rid"))
    override fun localAddr(): Optional<String> = Optional.of("tcp://127.0.0.1:29010")
    override fun remoteAddr(): Optional<String> = Optional.of("tcp://127.0.0.1:39010")
    override fun client(): ZLinkSessionClient = RecordingSessionClient()
    override fun actors(): RecordingSessionActors = actors
    override fun closeAsync(): CompletionStage<Void> = CompletableFuture.completedFuture(null)
}
