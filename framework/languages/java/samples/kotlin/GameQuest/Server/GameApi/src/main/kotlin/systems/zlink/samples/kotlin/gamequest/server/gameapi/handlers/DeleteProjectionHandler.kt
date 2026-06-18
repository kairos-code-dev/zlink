package systems.zlink.samples.kotlin.gamequest.server.gameapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.gamequest.server.gameapi.contracts.DeleteProjectionReq
import systems.zlink.samples.kotlin.gamequest.server.gameapi.contracts.DeleteProjectionRes
import systems.zlink.samples.kotlin.gamequest.server.gameapi.store.GameQuestStore

/** Self-check: deletes a single quest projection entry to test rebuild. */
@ZLinkHandlerGroup("gameapi")
class DeleteProjectionHandler(
    private val store: GameQuestStore,
) : ZLinkRequestHandler<DeleteProjectionReq, DeleteProjectionRes> {
    override fun handle(request: DeleteProjectionReq, context: ZLinkRequestContext): DeleteProjectionRes {
        store.deleteProjection(request.playerId, request.questId)
        return DeleteProjectionRes(true)
    }
}
