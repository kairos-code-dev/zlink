package systems.zlink.samples.kotlin.gamequest.server.questmission.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.gamequest.server.questmission.application.QuestEventProcessor
import systems.zlink.samples.kotlin.gamequest.shared.contracts.SyncQuestProgressReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.SyncQuestProgressRes

/** Mission action channel handler: reconciles a player from the GameApi snapshot. */
@ZLinkHandlerGroup("gamequest-mission")
class SyncQuestProgressHandler(
    private val processor: QuestEventProcessor,
) : ZLinkSuspendingRequestHandler<SyncQuestProgressReq, SyncQuestProgressRes> {
    override suspend fun handle(request: SyncQuestProgressReq, context: ZLinkRequestContext): SyncQuestProgressRes =
        processor.sync(request)
}
