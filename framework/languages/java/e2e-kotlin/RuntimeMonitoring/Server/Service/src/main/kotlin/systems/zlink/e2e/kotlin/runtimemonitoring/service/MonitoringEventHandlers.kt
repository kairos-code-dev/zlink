package systems.zlink.e2e.kotlin.runtimemonitoring.service

import systems.zlink.framework.monitoring.ZLinkRegistryEvent
import systems.zlink.framework.monitoring.ZLinkRuntimeEventHandler
import systems.zlink.framework.monitoring.ZLinkSocketEvent
import systems.zlink.framework.monitoring.ZLinkSpotEvent

class MonitoringEventHandlers private constructor() {
    class SocketRecorder(
        private val state: EvidenceState,
    ) : ZLinkRuntimeEventHandler<ZLinkSocketEvent> {
        override fun handle(event: ZLinkSocketEvent) {
            state.record(
                "socket",
                event.sourceName(),
                event.event().name,
                "${event.localAddr()}|${event.remoteAddr()}|${event.routingId().map { it.toString() }.orElse("")}",
            )
        }
    }

    class FailingSocketRecorder(
        private val state: EvidenceState,
    ) : ZLinkRuntimeEventHandler<ZLinkSocketEvent> {
        override fun handle(event: ZLinkSocketEvent) {
            state.record(
                "monitoring",
                event.sourceName(),
                "HandlerFailureInjected",
                event.event().name,
            )
            throw IllegalStateException("intentional monitoring handler failure")
        }
    }

    class RegistryRecorder(
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

    class SpotRecorder(
        private val state: EvidenceState,
    ) : ZLinkRuntimeEventHandler<ZLinkSpotEvent> {
        override fun handle(event: ZLinkSpotEvent) {
            val timerDetail = event.timerDiagnostic().map { "|timer=$it" }.orElse("")
            state.record(
                "spot",
                event.sourceName(),
                event.event().name,
                "peers=${event.peers().size}|subjects=${event.subjects().size}$timerDetail",
            )
        }
    }
}
