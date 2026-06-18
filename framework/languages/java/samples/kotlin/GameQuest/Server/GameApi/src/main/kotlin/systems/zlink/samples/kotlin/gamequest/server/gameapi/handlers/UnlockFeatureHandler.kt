package systems.zlink.samples.kotlin.gamequest.server.gameapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.gamequest.server.gameapi.application.GameplayActionService
import systems.zlink.samples.kotlin.gamequest.shared.contracts.UnlockFeatureReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.UnlockFeatureRes

@ZLinkHandlerGroup("gameapi")
class UnlockFeatureHandler(
    private val actions: GameplayActionService,
) : ZLinkSuspendingRequestHandler<UnlockFeatureReq, UnlockFeatureRes> {
    override suspend fun handle(request: UnlockFeatureReq, context: ZLinkRequestContext): UnlockFeatureRes =
        actions.unlockFeature(request)
}
