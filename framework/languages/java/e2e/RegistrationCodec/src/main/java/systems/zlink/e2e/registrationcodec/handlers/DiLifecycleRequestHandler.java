package systems.zlink.e2e.registrationcodec.handlers;

import org.springframework.beans.factory.ObjectProvider;
import systems.zlink.e2e.registrationcodec.Contracts;
import systems.zlink.e2e.registrationcodec.DiScopedDependency;
import systems.zlink.e2e.registrationcodec.DiSingletonDependency;
import systems.zlink.e2e.registrationcodec.ScenarioState;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class DiLifecycleRequestHandler
    implements ZLinkRequestHandler<Contracts.DiLifecycleRequest, Contracts.DiLifecycleReply> {
    private final ObjectProvider<DiScopedDependency> scoped;
    private final DiSingletonDependency singleton;
    private final ScenarioState state;

    public DiLifecycleRequestHandler(
        ObjectProvider<DiScopedDependency> scoped,
        DiSingletonDependency singleton,
        ScenarioState state) {
        this.scoped = scoped;
        this.singleton = singleton;
        this.state = state;
    }

    @Override
    public Contracts.DiLifecycleReply handle(
        Contracts.DiLifecycleRequest request,
        ZLinkRequestContext context) {
        int scopedId;
        try (DiScopedDependency dependency = scoped.getObject()) {
            scopedId = dependency.id();
            state.record(
                "DI",
                context.packetName().orElse("DiLifecycle"),
                scopedId + ":" + singleton.id() + ":" + request.value());
        }
        return new Contracts.DiLifecycleReply(
            "echo:" + request.value(),
            scopedId,
            singleton.id(),
            state.diDisposeCount());
    }
}
