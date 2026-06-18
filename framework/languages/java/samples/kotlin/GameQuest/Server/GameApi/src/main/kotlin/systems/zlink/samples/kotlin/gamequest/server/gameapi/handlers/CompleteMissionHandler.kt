package systems.zlink.samples.kotlin.gamequest.server.gameapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.gamequest.server.gameapi.application.GameplayActionService
import systems.zlink.samples.kotlin.gamequest.shared.contracts.CompleteMissionReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.CompleteMissionRes

@ZLinkHandlerGroup("gameapi")
class CompleteMissionHandler(
    private val actions: GameplayActionService,
) : ZLinkRequestHandler<CompleteMissionReq, CompleteMissionRes> {
    override fun handle(request: CompleteMissionReq, context: ZLinkRequestContext): CompleteMissionRes =
        actions.completeMission(request)
}
