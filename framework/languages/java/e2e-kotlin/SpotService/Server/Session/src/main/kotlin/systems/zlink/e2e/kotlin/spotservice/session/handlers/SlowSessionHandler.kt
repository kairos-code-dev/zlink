package systems.zlink.e2e.kotlin.spotservice.session.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionDispatchContext
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler

class SlowSessionHandler(
    private val evidence: ScenarioState
) : ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.SlowSessionReq> {
    override fun packetName(): String = "SlowSessionReq"

    override fun messageType(): Class<Contracts.SlowSessionReq> = Contracts.SlowSessionReq::class.java

    override fun handle(
        context: ZLinkSessionContext,
        dispatch: ZLinkSessionDispatchContext,
        request: Contracts.SlowSessionReq
    ) {
        try {
            Thread.sleep(request.delayMilliseconds.coerceAtLeast(0).toLong())
        } catch (_: InterruptedException) {
            evidence.record("SlowSessionInterrupted", "session", request.value)
        }
        evidence.record("SlowSessionHandled", "session", request.value)
        try {
            context.client()
                .reply(Contracts.SlowSessionRes(request.value))
                .await()
        } catch (error: Exception) {
            evidence.record("SlowSessionReplyFailed", "session", "${request.value}/${error.javaClass.simpleName}")
        }
    }
}
