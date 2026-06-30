package systems.zlink.e2e.kotlin.resiliencelifecycle.handlers

import systems.zlink.e2e.kotlin.resiliencelifecycle.Contracts
import systems.zlink.e2e.kotlin.resiliencelifecycle.ScenarioState
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
class WorkRequestHandler(
    private val state: ScenarioState,
) : ZLinkRequestHandler<Contracts.WorkRequest, Contracts.WorkReply> {
    override fun handle(
        request: Contracts.WorkRequest,
        context: ZLinkRequestContext,
    ): Contracts.WorkReply {
        when {
            state.grayFailure() && request.value().startsWith("b6-gray-") -> {
                state.record("GrayFailureInjected", request.value())
                throw IllegalStateException("gray failure")
            }
            request.value() == "slow" -> {
                state.record("SlowStarted", request.value())
                state.awaitSlowRelease()
                state.record("SlowCompleted", request.value())
            }
            request.value() == "timeout" -> {
                state.record("TimeoutStarted", request.value())
                sleep(1500)
                state.record("TimeoutCompleted", request.value())
            }
            else -> state.record("WorkRequest", request.value())
        }
        return Contracts.WorkReply("work:${request.value()}", state.providerRid())
    }

    private fun sleep(millis: Long) {
        try {
            Thread.sleep(millis)
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("interrupted", error)
        }
    }
}
