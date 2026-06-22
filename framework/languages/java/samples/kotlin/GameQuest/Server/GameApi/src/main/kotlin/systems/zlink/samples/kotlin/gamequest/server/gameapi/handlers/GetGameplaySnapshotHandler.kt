package systems.zlink.samples.kotlin.gamequest.server.gameapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.gamequest.server.gameapi.infrastructure.store.GameQuestStore
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GetGameplaySnapshotReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GetGameplaySnapshotRes

@ZLinkHandlerGroup("gameapi")
class GetGameplaySnapshotHandler(
    private val store: GameQuestStore,
) : ZLinkSuspendingRequestHandler<GetGameplaySnapshotReq, GetGameplaySnapshotRes> {
    override suspend fun handle(request: GetGameplaySnapshotReq, context: ZLinkRequestContext): GetGameplaySnapshotRes =
        store.readSnapshot(request.playerId)
}
