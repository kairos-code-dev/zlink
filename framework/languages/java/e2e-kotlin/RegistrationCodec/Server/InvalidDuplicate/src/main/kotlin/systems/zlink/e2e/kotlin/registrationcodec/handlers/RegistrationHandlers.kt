package systems.zlink.e2e.kotlin.registrationcodec.handlers

import systems.zlink.e2e.kotlin.registrationcodec.EchoManualRequest
import systems.zlink.e2e.kotlin.registrationcodec.EchoReply
import systems.zlink.e2e.kotlin.registrationcodec.ScenarioState
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler

class ManualRequestHandler(
    private val state: ScenarioState,
) : ZLinkRequestHandler<EchoManualRequest, EchoReply> {
    override fun handle(
        message: EchoManualRequest,
        context: ZLinkRequestContext,
    ): EchoReply {
        state.record("Request", "DuplicatePacket", message.value)
        return EchoReply("echo:${message.value}", "manual")
    }
}
