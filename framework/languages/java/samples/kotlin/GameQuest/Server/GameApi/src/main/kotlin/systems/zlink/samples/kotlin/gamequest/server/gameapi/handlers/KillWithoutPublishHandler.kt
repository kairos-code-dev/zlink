package systems.zlink.samples.kotlin.gamequest.server.gameapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.gamequest.server.gameapi.contracts.KillWithoutPublishReq
import systems.zlink.samples.kotlin.gamequest.server.gameapi.contracts.KillWithoutPublishRes
import systems.zlink.samples.kotlin.gamequest.server.gameapi.infrastructure.store.GameQuestStore

/** Self-check: records a kill in the store without publishing it to fanout. */
@ZLinkHandlerGroup("gameapi")
class KillWithoutPublishHandler(
    private val store: GameQuestStore,
) : ZLinkSuspendingRequestHandler<KillWithoutPublishReq, KillWithoutPublishRes> {
    override suspend fun handle(request: KillWithoutPublishReq, context: ZLinkRequestContext): KillWithoutPublishRes {
        store.addUnpublishedKill(request.playerId, 1)
        return KillWithoutPublishRes(true)
    }
}
