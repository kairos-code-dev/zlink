package systems.zlink.samples.kotlin.gamequest.server.gameapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.gamequest.server.gameapi.application.GameplayActionService
import systems.zlink.samples.kotlin.gamequest.shared.contracts.UnlockFeatureReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.UnlockFeatureRes

@ZLinkHandlerGroup("gameapi")
class UnlockFeatureHandler(
    private val actions: GameplayActionService,
) : ZLinkRequestHandler<UnlockFeatureReq, UnlockFeatureRes> {
    override fun handle(request: UnlockFeatureReq, context: ZLinkRequestContext): UnlockFeatureRes =
        actions.unlockFeature(request)
}
