package systems.zlink.samples.kotlin.gamequest.server.gameapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.gamequest.server.gameapi.contracts.RebuildProjectionReq
import systems.zlink.samples.kotlin.gamequest.server.gameapi.contracts.RebuildProjectionRes
import systems.zlink.samples.kotlin.gamequest.server.gameapi.store.GameQuestStore

/** Self-check: rebuilds a quest projection entry from the stored event stream. */
@ZLinkHandlerGroup("gameapi")
class RebuildProjectionHandler(
    private val store: GameQuestStore,
) : ZLinkRequestHandler<RebuildProjectionReq, RebuildProjectionRes> {
    override fun handle(request: RebuildProjectionReq, context: ZLinkRequestContext): RebuildProjectionRes =
        RebuildProjectionRes(store.rebuildProjection(request.playerId, request.questId))
}
