package systems.zlink.e2e.kotlin.resiliencelifecycle.handlers

import systems.zlink.e2e.kotlin.resiliencelifecycle.Contracts
import systems.zlink.e2e.kotlin.resiliencelifecycle.ScenarioState
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.channels.ZLinkSendHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
class WorkCommandHandler(
    private val state: ScenarioState,
) : ZLinkSendHandler<Contracts.WorkMsg> {
    override fun handle(
        message: Contracts.WorkMsg,
        context: ZLinkSendContext,
    ) {
        state.record("WorkMsg", message.value())
    }
}
