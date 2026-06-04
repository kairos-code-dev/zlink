package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers

import java.util.concurrent.CompletionStage
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.streams.ZLinkSessionActor
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionPacketHandler
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames

class CreateMatchSessionPacketHandler(
    private val channels: ZLinkClient,
) : ZLinkSessionPacketHandler<ZLinkSessionContext> {
    override fun packetName(): String = "CreateMatchReq"

    override fun handleAsync(
        context: ZLinkSessionContext,
        header: ZLinkStreamHeader,
        payload: Message,
    ): CompletionStage<Void> {
        val actor = requireSingleBoundActor(context)
        return channels.requestToChannel(SampleNames.PlayChannel, actor.actorId())
            .packetName("CreateMatchRoom")
            .submitAsync(String::class.java)
            .thenCompose { reply ->
                context.client()
                    .reply(reply)
                    .submitAsync()
            }
    }

    companion object {
        fun requireSingleBoundActor(context: ZLinkSessionContext): ZLinkSessionActor =
            when (context.actors().bound().size) {
                1 -> context.actors().bound()[0]
                0 -> throw IllegalStateException("Client must authenticate before using the session gateway")
                else -> throw IllegalStateException("Exactly one actor must be bound before using the session gateway")
            }
    }
}
