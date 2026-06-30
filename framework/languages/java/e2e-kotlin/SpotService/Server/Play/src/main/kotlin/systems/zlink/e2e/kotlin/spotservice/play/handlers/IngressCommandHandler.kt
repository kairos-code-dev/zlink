package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.spots.*
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.channels.ZLinkSendHandler

class IngressCommandHandler(
    private val state: ScenarioState,
) : ZLinkSendHandler<Contracts.OutboundMsg> {
    override fun handle(message: Contracts.OutboundMsg, context: ZLinkSendContext) {
        state.record("IngressCommand", "channel", message.value)
    }
}
