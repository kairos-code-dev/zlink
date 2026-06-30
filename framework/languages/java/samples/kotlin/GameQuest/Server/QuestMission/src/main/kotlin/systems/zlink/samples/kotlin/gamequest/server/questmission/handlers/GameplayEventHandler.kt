package systems.zlink.samples.kotlin.gamequest.server.questmission.handlers

import systems.zlink.framework.channels.ZLinkPublishContext
import systems.zlink.framework.kotlin.ZLinkSuspendingPublishHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.gamequest.server.configuration.SampleNames
import systems.zlink.samples.kotlin.gamequest.server.questmission.application.QuestEventProcessor
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GameplayEventMsg

/** Fanout subscriber: processes published gameplay events. Mirrors the .NET `GameplayEventHandler`. */
@ZLinkHandlerGroup("gamequest-gameplay")
class GameplayEventHandler(
    private val processor: QuestEventProcessor,
) : ZLinkSuspendingPublishHandler<GameplayEventMsg> {
    override suspend fun handle(message: GameplayEventMsg, context: ZLinkPublishContext) = run {
        if (context.topic() != SampleNames.GameplayTopic) {
            return
        }
        processor.process(message)
    }
}
