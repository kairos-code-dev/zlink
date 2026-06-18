package systems.zlink.samples.kotlin.gamequest.server.gameapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.gamequest.server.gameapi.application.GameplayActionService
import systems.zlink.samples.kotlin.gamequest.shared.contracts.EnterAreaReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.EnterAreaRes

@ZLinkHandlerGroup("gameapi")
class EnterAreaHandler(
    private val actions: GameplayActionService,
) : ZLinkRequestHandler<EnterAreaReq, EnterAreaRes> {
    override fun handle(request: EnterAreaReq, context: ZLinkRequestContext): EnterAreaRes =
        actions.enterArea(request)
}
