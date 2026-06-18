package systems.zlink.samples.kotlin.gamequest.server.gameapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.gamequest.server.gameapi.application.GameplayActionService
import systems.zlink.samples.kotlin.gamequest.shared.contracts.CollectItemReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.CollectItemRes

@ZLinkHandlerGroup("gameapi")
class CollectItemHandler(
    private val actions: GameplayActionService,
) : ZLinkSuspendingRequestHandler<CollectItemReq, CollectItemRes> {
    override suspend fun handle(request: CollectItemReq, context: ZLinkRequestContext): CollectItemRes =
        actions.collectItem(request)
}
