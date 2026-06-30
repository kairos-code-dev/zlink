package systems.zlink.e2e.registrationcodec.main.Handlers;

import org.springframework.beans.factory.ObjectProvider;
import systems.zlink.e2e.registrationcodec.shared.Contracts;
import systems.zlink.e2e.registrationcodec.main.Infrastructure.DiScopedDependency;
import systems.zlink.e2e.registrationcodec.main.Infrastructure.DiSingletonDependency;
import systems.zlink.e2e.registrationcodec.main.Infrastructure.EvidenceStore;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class DiLifecycleRequestHandler
    implements ZLinkRequestHandler<Contracts.DiLifecycleRequest, Contracts.DiLifecycleReply> {
    private final ObjectProvider<DiScopedDependency> scoped;
    private final DiSingletonDependency singleton;
    private final EvidenceStore state;

    public DiLifecycleRequestHandler(
        ObjectProvider<DiScopedDependency> scoped,
        DiSingletonDependency singleton,
        EvidenceStore state) {
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
