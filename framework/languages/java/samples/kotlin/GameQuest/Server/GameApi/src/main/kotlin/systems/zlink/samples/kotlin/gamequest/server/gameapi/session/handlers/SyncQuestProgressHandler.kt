package systems.zlink.samples.kotlin.gamequest.server.gameapi.session.handlers

import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionPacketHandler
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.samples.kotlin.gamequest.server.gameapi.application.GameplayActionService
import systems.zlink.samples.kotlin.gamequest.shared.contracts.SyncQuestProgressReq

/** Triggers a QuestMission reconciliation sync and replies over the stream. */
class SyncQuestProgressHandler(
    private val actions: GameplayActionService,
) : ZLinkSessionPacketHandler<ZLinkSessionContext> {
    override fun packetName(): String = "SyncQuestProgressReq"

    override fun handle(context: ZLinkSessionContext, header: ZLinkStreamHeader, payload: ZLinkMessage) {
        val request = payload.decode(SyncQuestProgressReq::class.java)
        context.client().reply(actions.sync(request.playerId)).await()
    }
}
