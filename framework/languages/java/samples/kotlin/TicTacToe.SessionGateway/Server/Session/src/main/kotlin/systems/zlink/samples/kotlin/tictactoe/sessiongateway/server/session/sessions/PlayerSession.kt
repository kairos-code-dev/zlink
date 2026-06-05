package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.streams.ZLinkSession
import systems.zlink.framework.streams.ZLinkSessionActor
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher
import systems.zlink.framework.streams.ZLinkStreamError
import systems.zlink.framework.streams.ZLinkStreamHeader

class PlayerSession(
    private val context: ZLinkSessionContext,
    private val handlers: ZLinkSessionPacketDispatcher<ZLinkSessionContext>,
    private val sessions: PlayerSessionDirectory,
) : ZLinkSession {
    override fun context(): ZLinkSessionContext = context

    override fun onConnectedAsync(): CompletionStage<Void> =
        CompletableFuture.completedFuture(null)

    override fun onDisconnectedAsync(): CompletionStage<Void> =
        context.actors().bound().forEach { actor ->
            sessions.unbind(actor.actorId(), context)
        }.let { CompletableFuture.completedFuture(null) }

    override fun onErrorAsync(error: ZLinkStreamError): CompletionStage<Void> =
        CompletableFuture.completedFuture(null)

    override fun onDispatchAsync(
        header: ZLinkStreamHeader,
        payload: Message,
    ): CompletionStage<Void> =
        handlers.tryHandleAsync(context, header, payload)
            .thenCompose { handled ->
                if (handled) {
                    CompletableFuture.completedFuture(null)
                } else {
                    val actor = requireSingleBoundActor(header.packetName())
                    actor.relayAsync(header, payload)
                }
            }

    private fun requireSingleBoundActor(packetName: String): ZLinkSessionActor =
        when (context.actors().bound().size) {
            1 -> context.actors().bound()[0]
            0 -> throw IllegalStateException(
                "Client must authenticate before relaying packet '$packetName'",
            )
            else -> throw IllegalStateException(
                "Exactly one actor must be bound before relaying packet '$packetName'",
            )
        }
}
