package systems.zlink.e2e.kotlin.pubsub.subscriber

import systems.zlink.e2e.kotlin.pubsub.shared.Contracts
import systems.zlink.e2e.kotlin.pubsub.shared.EventMsg
import systems.zlink.framework.channels.ZLinkPublishContext
import systems.zlink.framework.channels.ZLinkPublishHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
class EventMsgHandler(
    private val state: EvidenceStore,
) : ZLinkPublishHandler<EventMsg> {
    override fun handle(
        message: EventMsg,
        context: ZLinkPublishContext,
    ) {
        if (!state.accepts(context.topic())) {
            return
        }
        state.delayIfConfigured(message.scenario)
        state.record(
            "EventMsg",
            context.topic(),
            message.scenario,
            message.sequence,
            message.value,
        )
    }
}
