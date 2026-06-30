package systems.zlink.e2e.kotlin.registrationcodec.handlers

import systems.zlink.e2e.kotlin.registrationcodec.EchoManualReq
import systems.zlink.e2e.kotlin.registrationcodec.EchoManualRes
import systems.zlink.e2e.kotlin.registrationcodec.ScenarioState
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler

class ManualRequestHandler(
    private val state: ScenarioState,
) : ZLinkRequestHandler<EchoManualReq, EchoManualRes> {
    override fun handle(
        message: EchoManualReq,
        context: ZLinkRequestContext,
    ): EchoManualRes {
        state.record("Request", "DuplicatePacket", message.value)
        return EchoManualRes("echo:${message.value}", "manual")
    }
}
