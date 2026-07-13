package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.spots.*
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler

class NoopIngressHandler(
    private val state: ScenarioState,
) : ZLinkSuspendingRequestHandler<String, String> {
    override suspend fun handle(request: String, context: ZLinkRequestContext): String {
        state.record("IngressRequest", "channel", request)
        return request
    }
}
