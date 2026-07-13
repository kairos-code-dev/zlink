package systems.zlink.e2e.kotlin.runtimemonitoring.service

import systems.zlink.e2e.kotlin.runtimemonitoring.Contracts
import systems.zlink.e2e.kotlin.runtimemonitoring.Env
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
class WorkRequestHandler : ZLinkSuspendingRequestHandler<Contracts.WorkReq, Contracts.WorkRes> {
    override suspend fun handle(
        request: Contracts.WorkReq,
        context: ZLinkRequestContext,
    ): Contracts.WorkRes {
        return Contracts.WorkRes(
            "work:${request.value}",
            Env.get("ZLINK_KOTLIN_E2E_RID", "svc-a"),
        )
    }
}
