package systems.zlink.samples.kotlin.gamequest.server.gameapi.session.handlers

import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionPacketHandler
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.samples.kotlin.gamequest.server.gameapi.session.GameQuestSessionRegistry
import systems.zlink.samples.kotlin.gamequest.server.gameapi.session.StreamPayloads
import systems.zlink.samples.kotlin.gamequest.server.gameapi.infrastructure.store.GameQuestStore
import systems.zlink.samples.kotlin.gamequest.shared.contracts.SubscribeQuestReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.SubscribeQuestRes

/** Binds the player session and replies with the current quest projection. */
class SubscribeQuestHandler(
    private val store: GameQuestStore,
    private val registry: GameQuestSessionRegistry,
) : ZLinkSessionPacketHandler<ZLinkSessionContext> {
    override fun packetName(): String = "SubscribeQuestReq"

    override fun handle(context: ZLinkSessionContext, header: ZLinkStreamHeader, payload: Message) {
        val request = StreamPayloads.decode(header, payload, SubscribeQuestReq::class.java)
        System.err.printf(
            "gamequest session subscribe session=%s player=%s%n",
            context.sessionId(), request.playerId,
        )
        store.bindSession(registry.bind(request.playerId, context))
        System.err.printf(
            "gamequest session binding stored session=%s player=%s%n",
            context.sessionId(), request.playerId,
        )
        context.client()
            .reply(SubscribeQuestRes(store.readProjection(request.playerId)))
            .await()
        System.err.printf(
            "gamequest session subscribed session=%s player=%s%n",
            context.sessionId(), request.playerId,
        )
    }
}
