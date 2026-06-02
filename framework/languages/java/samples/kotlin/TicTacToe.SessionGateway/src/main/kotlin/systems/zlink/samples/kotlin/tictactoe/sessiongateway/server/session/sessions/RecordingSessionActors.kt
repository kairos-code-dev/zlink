package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions

import java.util.Optional
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.streams.ZLinkSessionActor
import systems.zlink.framework.streams.ZLinkSessionActors
import systems.zlink.framework.streams.ZLinkStreamHeader

class RecordingSessionActors : ZLinkSessionActors {
    private val bound = mutableListOf<ZLinkSessionActor>()

    override fun bound(): List<ZLinkSessionActor> = bound.toList()

    override fun bindAsync(actor: ZLinkActor): CompletionStage<ZLinkSessionActor> =
        bindAsync(ZLinkActorRef(RoutingId.from("local-node"), actor.actorId(), 1))

    override fun bindAsync(actor: ZLinkActorRef): CompletionStage<ZLinkSessionActor> {
        val sessionActor = BoundActor(actor)
        bound += sessionActor
        return CompletableFuture.completedFuture(sessionActor)
    }

    override fun find(actorId: String): Optional<ZLinkSessionActor> =
        Optional.ofNullable(bound.firstOrNull { it.actorId() == actorId })

    private data class BoundActor(private val ref: ZLinkActorRef) : ZLinkSessionActor {
        override fun actorId(): String = ref.actorId()
        override fun ref(): ZLinkActorRef = ref
        override fun relayAsync(header: ZLinkStreamHeader, payload: Message): CompletionStage<Void> =
            CompletableFuture.completedFuture(null)

        override fun notifyDisconnectedAsync(): CompletionStage<Void> =
            CompletableFuture.completedFuture(null)
    }
}
