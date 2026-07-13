package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.spots.*
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.kotlin.ZLinkSuspendingSendHandler

class IngressCommandHandler(
    private val state: ScenarioState,
) : ZLinkSuspendingSendHandler<Contracts.OutboundMsg> {
    override suspend fun handle(message: Contracts.OutboundMsg, context: ZLinkSendContext) {
        state.record("IngressCommand", "channel", message.value)
    }
}
