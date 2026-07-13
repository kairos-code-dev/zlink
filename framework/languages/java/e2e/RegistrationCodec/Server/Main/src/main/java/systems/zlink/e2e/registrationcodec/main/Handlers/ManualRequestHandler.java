package systems.zlink.e2e.registrationcodec.main.Handlers;

import systems.zlink.e2e.registrationcodec.shared.Contracts;
import systems.zlink.e2e.registrationcodec.main.Infrastructure.EvidenceStore;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class ManualRequestHandler
    implements ZLinkRequestHandler<Contracts.EchoManualReq, Contracts.EchoRes> {
    private final EvidenceStore state;

    public ManualRequestHandler(EvidenceStore state) {
        this.state = state;
    }

    @Override
    public java.util.concurrent.CompletionStage<Contracts.EchoRes> handle(
        Contracts.EchoManualReq request,
        ZLinkRequestContext context) {
        state.record("Request", context.packetName().orElse("EchoManual"), request.value());
        return java.util.concurrent.CompletableFuture.completedFuture(
            new Contracts.EchoRes("echo:" + request.value(), "manual"));
    }
}
