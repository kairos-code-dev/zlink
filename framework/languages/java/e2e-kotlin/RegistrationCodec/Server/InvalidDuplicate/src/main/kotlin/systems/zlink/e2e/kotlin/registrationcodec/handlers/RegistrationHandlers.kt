package systems.zlink.e2e.kotlin.registrationcodec.handlers

import systems.zlink.e2e.kotlin.registrationcodec.EchoManualReq
import systems.zlink.e2e.kotlin.registrationcodec.EchoManualRes
import systems.zlink.e2e.kotlin.registrationcodec.ScenarioState
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler

class ManualRequestHandler(
    private val state: ScenarioState,
) : ZLinkSuspendingRequestHandler<EchoManualReq, EchoManualRes> {
    override suspend fun handle(
        request: EchoManualReq,
        context: ZLinkRequestContext,
    ): EchoManualRes {
        state.record("Request", "DuplicatePacket", request.value)
        return EchoManualRes("echo:${request.value}", "manual")
    }
}
