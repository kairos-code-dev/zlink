package systems.zlink.e2e.runtimemonitoring.service.handlers;

import systems.zlink.e2e.runtimemonitoring.service.support.EvidenceState;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;
import systems.zlink.e2e.runtimemonitoring.shared.Env;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
public final class WorkReqHandler
    implements ZLinkRequestHandler<Contracts.WorkReq, Contracts.WorkRes> {
    private final EvidenceState evidence;

    public WorkReqHandler(EvidenceState evidence) {
        this.evidence = evidence;
    }

    @Override
    public java.util.concurrent.CompletionStage<Contracts.WorkRes> handle(
        Contracts.WorkReq request,
        ZLinkRequestContext context) {
        evidence.record("work", evidence.rid(), "WorkReq", request.value());
        return java.util.concurrent.CompletableFuture.completedFuture(new Contracts.WorkRes(
            "work:" + request.value(),
            Env.get("routingId", "svc-a")));
    }
}
