package systems.zlink.e2e.resiliencelifecycle.provider.handlers;

import systems.zlink.e2e.resiliencelifecycle.provider.infrastructure.ScenarioState;
import systems.zlink.framework.monitoring.ZLinkRuntimeErrorEvent;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventHandler;

public final class RuntimeErrorEvidenceHandler
    implements ZLinkRuntimeEventHandler<ZLinkRuntimeErrorEvent> {
    private final ScenarioState state;

    public RuntimeErrorEvidenceHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public void handle(ZLinkRuntimeErrorEvent event) {
        state.record("RuntimeError", event.event().name() + "/" + event.callbackName());
    }
}
