package systems.zlink.e2e.kotlin.resiliencelifecycle.handlers

import systems.zlink.e2e.kotlin.resiliencelifecycle.Contracts
import systems.zlink.e2e.kotlin.resiliencelifecycle.ScenarioState
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.kotlin.ZLinkSuspendingSendHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
class WorkCommandHandler(
    private val state: ScenarioState,
) : ZLinkSuspendingSendHandler<Contracts.WorkMsg> {
    override suspend fun handle(
        message: Contracts.WorkMsg,
        context: ZLinkSendContext,
    ) {
        state.record("WorkMsg", message.value())
    }
}
