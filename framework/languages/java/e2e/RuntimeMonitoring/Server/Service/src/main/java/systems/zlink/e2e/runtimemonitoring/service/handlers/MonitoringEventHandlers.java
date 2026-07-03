package systems.zlink.e2e.runtimemonitoring.service.handlers;

import systems.zlink.e2e.runtimemonitoring.service.support.EvidenceState;
import systems.zlink.framework.monitoring.ZLinkLocationRuntimeEvent;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventHandler;
import systems.zlink.framework.monitoring.ZLinkSocketEvent;
import systems.zlink.framework.monitoring.ZLinkSpotEvent;

public final class MonitoringEventHandlers {
    private MonitoringEventHandlers() {
    }

    public static final class SocketRecorder implements ZLinkRuntimeEventHandler<ZLinkSocketEvent> {
        private final EvidenceState state;

        public SocketRecorder(EvidenceState state) {
            this.state = state;
        }

        @Override
        public void handle(ZLinkSocketEvent event) {
            state.record(
                "socket",
                event.sourceName(),
                event.event().name(),
                event.localAddr() + "|" + event.remoteAddr()
                    + "|" + event.routingId().map(Object::toString).orElse(""));
        }
    }

    public static final class FailingSocketRecorder
        implements ZLinkRuntimeEventHandler<ZLinkSocketEvent> {
        private final EvidenceState state;

        public FailingSocketRecorder(EvidenceState state) {
            this.state = state;
        }

        @Override
        public void handle(ZLinkSocketEvent event) {
            state.record(
                "monitoring",
                event.sourceName(),
                "HandlerFailureInjected",
                event.event().name());
            throw new IllegalStateException("intentional monitoring handler failure");
        }
    }

    public static final class LocationRuntimeRecorder
        implements ZLinkRuntimeEventHandler<ZLinkLocationRuntimeEvent> {
        private final EvidenceState state;

        public LocationRuntimeRecorder(EvidenceState state) {
            this.state = state;
        }

        @Override
        public void handle(ZLinkLocationRuntimeEvent event) {
            state.record(
                "location",
                event.sourceName(),
                event.event().name(),
                "topology=" + (event.topology() == null ? 0 : event.topology().size())
                    + "|summary=" + (event.serviceSummary() == null ? 0 : event.serviceSummary().size()));
        }
    }

    public static final class SpotRecorder implements ZLinkRuntimeEventHandler<ZLinkSpotEvent> {
        private final EvidenceState state;

        public SpotRecorder(EvidenceState state) {
            this.state = state;
        }

        @Override
        public void handle(ZLinkSpotEvent event) {
            state.record(
                "spot",
                event.sourceName(),
                event.event().name(),
                "peers=" + event.peers().size() + "|subjects=" + event.subjects().size()
                    + event.timerDiagnostic().map(value -> "|timer=" + value).orElse(""));
        }
    }
}
