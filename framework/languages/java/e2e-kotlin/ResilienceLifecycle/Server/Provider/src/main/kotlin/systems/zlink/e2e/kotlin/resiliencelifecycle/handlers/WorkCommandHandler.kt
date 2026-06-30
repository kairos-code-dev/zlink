package systems.zlink.e2e.kotlin.resiliencelifecycle.handlers

import systems.zlink.e2e.kotlin.resiliencelifecycle.Contracts
import systems.zlink.e2e.kotlin.resiliencelifecycle.ScenarioState
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.channels.ZLinkSendHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
class WorkCommandHandler(
    private val state: ScenarioState,
) : ZLinkSendHandler<Contracts.WorkCommand> {
    override fun handle(
        message: Contracts.WorkCommand,
        context: ZLinkSendContext,
    ) {
        state.record("WorkCommand", message.value())
    }
}
