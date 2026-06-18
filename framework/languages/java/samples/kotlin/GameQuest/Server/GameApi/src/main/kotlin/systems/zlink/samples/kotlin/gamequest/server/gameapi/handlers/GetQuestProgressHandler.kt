package systems.zlink.samples.kotlin.gamequest.server.gameapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.gamequest.server.gameapi.store.GameQuestStore
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GetQuestProgressReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GetQuestProgressRes

@ZLinkHandlerGroup("gameapi")
class GetQuestProgressHandler(
    private val store: GameQuestStore,
) : ZLinkRequestHandler<GetQuestProgressReq, GetQuestProgressRes> {
    override fun handle(request: GetQuestProgressReq, context: ZLinkRequestContext): GetQuestProgressRes =
        GetQuestProgressRes(store.readProjection(request.playerId))
}
