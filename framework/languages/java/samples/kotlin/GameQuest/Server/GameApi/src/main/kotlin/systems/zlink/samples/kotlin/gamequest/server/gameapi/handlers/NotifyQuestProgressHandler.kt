package systems.zlink.samples.kotlin.gamequest.server.gameapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.gamequest.server.gameapi.session.GameQuestSessionRegistry
import systems.zlink.samples.kotlin.gamequest.shared.contracts.NotifyQuestProgressReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.NotifyQuestProgressRes

/** Receives projection notifications from QuestMission and pushes them to the bound stream session. */
@ZLinkHandlerGroup("gameapi")
class NotifyQuestProgressHandler(
    private val registry: GameQuestSessionRegistry,
) : ZLinkSuspendingRequestHandler<NotifyQuestProgressReq, NotifyQuestProgressRes> {
    override suspend fun handle(request: NotifyQuestProgressReq, context: ZLinkRequestContext): NotifyQuestProgressRes =
        NotifyQuestProgressRes(registry.notify(request))
}
