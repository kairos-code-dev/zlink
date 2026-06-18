package systems.zlink.samples.kotlin.gamequest.server.gameapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.gamequest.server.gameapi.application.GameplayActionService
import systems.zlink.samples.kotlin.gamequest.shared.contracts.KillMonsterReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.KillMonsterRes

@ZLinkHandlerGroup("gameapi")
class KillMonsterHandler(
    private val actions: GameplayActionService,
) : ZLinkSuspendingRequestHandler<KillMonsterReq, KillMonsterRes> {
    override suspend fun handle(request: KillMonsterReq, context: ZLinkRequestContext): KillMonsterRes =
        actions.killMonster(request)
}
