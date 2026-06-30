package systems.zlink.e2e.kotlin.registrationcodec.client.scenarios

import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.EchoManualRequest
import systems.zlink.e2e.kotlin.registrationcodec.EchoReply
import systems.zlink.e2e.kotlin.registrationcodec.client.support.EvidenceText
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ScenarioAssert
import systems.zlink.framework.channels.ZLinkClient

class RcA5FilterOrderingScenario(
    private val client: ZLinkClient,
    private val evidence: EvidenceText,
    private val assert: ScenarioAssert,
) {
    fun run() {
        val filtered = client.requestToChannel(Contracts.CHANNEL, EchoManualRequest("filter-order-request"))
            .packetName("EchoManual")
            .timeout(requestTimeout)
            .await(EchoReply::class.java)
        assert.that(filtered.value == "echo:filter-order-request", "RC-A5 request mismatch")
        evidence.waitForFilterOrder("filter-order-request")
        println("scenario RC-A5 passed")
    }
}
