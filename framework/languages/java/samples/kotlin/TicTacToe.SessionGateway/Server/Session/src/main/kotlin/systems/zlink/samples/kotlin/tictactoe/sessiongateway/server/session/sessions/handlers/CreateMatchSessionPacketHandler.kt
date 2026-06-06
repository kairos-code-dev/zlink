package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.kotlin.ZLinkCoroutineSessionPacketHandler
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.streams.ZLinkSessionActor
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames

class CreateMatchSessionPacketHandler(
    private val channels: ZLinkClient,
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSessionPacketHandler<ZLinkSessionContext>(coroutines, "CreateMatchReq") {
    override suspend fun handle(
        context: ZLinkSessionContext,
        header: ZLinkStreamHeader,
        payload: Message,
    ) {
        val actor = requireSingleBoundActor(context)
        val reply = channels.requestToChannel(SampleNames.PlayChannel, actor.actorId())
            .packetName("CreateMatchRoom")
            .submitAsync(String::class.java)
            .await()
        context.client()
            .reply(reply)
            .submitAsync()
            .await()
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
