package systems.zlink.e2e.kotlin.pubsub

import systems.zlink.framework.channels.ZLinkPublishContext
import systems.zlink.framework.channels.ZLinkPublishHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
class EventNotifyHandler(
    private val state: ScenarioState,
) : ZLinkPublishHandler<EventNotify> {
    override fun handle(
        message: EventNotify,
        context: ZLinkPublishContext,
    ) {
        if (!state.accepts(context.topic())) {
            return
        }
        state.delayIfConfigured(message.scenario)
        state.record(
            "EventNotify",
            context.topic(),
            message.scenario,
            message.sequence,
            message.value,
        )
    }
}
