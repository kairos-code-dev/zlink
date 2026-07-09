package systems.zlink.e2e.kotlin.resiliencelifecycle

import systems.zlink.framework.monitoring.ZLinkRuntimeErrorEvent
import systems.zlink.framework.monitoring.ZLinkRuntimeEventHandler

class RuntimeErrorEvidenceHandler(
    private val state: ScenarioState,
) : ZLinkRuntimeEventHandler<ZLinkRuntimeErrorEvent> {
    override fun handle(event: ZLinkRuntimeErrorEvent) {
        state.record(
            "RuntimeError",
            "${event.event()}/${event.callbackName()}/${event.exceptionType()}/${event.message().orElse("")}",
        )
    }
}
