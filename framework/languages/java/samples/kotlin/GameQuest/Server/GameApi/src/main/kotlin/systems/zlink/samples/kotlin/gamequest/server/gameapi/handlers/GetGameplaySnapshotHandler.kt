package systems.zlink.samples.kotlin.gamequest.server.gameapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.gamequest.server.gameapi.store.GameQuestStore
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GetGameplaySnapshotReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GetGameplaySnapshotRes

@ZLinkHandlerGroup("gameapi")
class GetGameplaySnapshotHandler(
    private val store: GameQuestStore,
) : ZLinkRequestHandler<GetGameplaySnapshotReq, GetGameplaySnapshotRes> {
    override fun handle(request: GetGameplaySnapshotReq, context: ZLinkRequestContext): GetGameplaySnapshotRes =
        store.readSnapshot(request.playerId)
}
