package systems.zlink.e2e.kotlin.runtimemonitoring.service

import systems.zlink.framework.monitoring.ZLinkLocationRuntimeEvent
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

    class LocationRuntimeRecorder(
        private val state: EvidenceState,
    ) : ZLinkRuntimeEventHandler<ZLinkLocationRuntimeEvent> {
        override fun handle(event: ZLinkLocationRuntimeEvent) {
            state.record(
                "location",
                event.sourceName(),
                event.event().name,
                "topology=${event.topology()?.size ?: 0}|summary=${event.serviceSummary()?.size ?: 0}",
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
