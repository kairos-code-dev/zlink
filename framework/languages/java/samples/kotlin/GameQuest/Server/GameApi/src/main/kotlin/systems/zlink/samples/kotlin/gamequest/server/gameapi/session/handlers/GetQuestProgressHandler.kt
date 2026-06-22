package systems.zlink.samples.kotlin.gamequest.server.gameapi.session.handlers

import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionPacketHandler
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.samples.kotlin.gamequest.server.gameapi.session.StreamPayloads
import systems.zlink.samples.kotlin.gamequest.server.gameapi.infrastructure.store.GameQuestStore
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GetQuestProgressReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GetQuestProgressRes

/** Replies with the current quest projection for the player over the stream. */
class GetQuestProgressHandler(
    private val store: GameQuestStore,
) : ZLinkSessionPacketHandler<ZLinkSessionContext> {
    override fun packetName(): String = "GetQuestProgressReq"

    override fun handle(context: ZLinkSessionContext, header: ZLinkStreamHeader, payload: Message) {
        val request = StreamPayloads.decode(header, payload, GetQuestProgressReq::class.java)
        context.client()
            .reply(GetQuestProgressRes(store.readProjection(request.playerId)))
            .await()
    }
}
