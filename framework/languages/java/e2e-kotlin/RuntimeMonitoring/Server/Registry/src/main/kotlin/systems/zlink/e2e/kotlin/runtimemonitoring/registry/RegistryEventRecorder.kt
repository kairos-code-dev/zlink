package systems.zlink.e2e.kotlin.runtimemonitoring.registry

import systems.zlink.framework.monitoring.ZLinkRegistryEvent
import systems.zlink.framework.monitoring.ZLinkRuntimeEventHandler

class RegistryEventRecorder(
    private val state: EvidenceState,
) : ZLinkRuntimeEventHandler<ZLinkRegistryEvent> {
    override fun handle(event: ZLinkRegistryEvent) {
        state.record(
            "registry",
            event.sourceName(),
            event.event().name,
            "topology=${event.topology().size}|summary=${event.serviceSummary().size}",
        )
    }
}
