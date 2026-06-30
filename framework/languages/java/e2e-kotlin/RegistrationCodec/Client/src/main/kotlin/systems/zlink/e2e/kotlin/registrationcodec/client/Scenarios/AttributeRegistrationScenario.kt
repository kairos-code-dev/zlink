package systems.zlink.e2e.kotlin.registrationcodec.client.scenarios

import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.EchoAttrCommand
import systems.zlink.e2e.kotlin.registrationcodec.EchoAttrRequest
import systems.zlink.e2e.kotlin.registrationcodec.EchoReply
import systems.zlink.e2e.kotlin.registrationcodec.client.support.EvidenceText
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ScenarioAssert
import systems.zlink.framework.channels.ZLinkClient

class AttributeRegistrationScenario(
    private val client: ZLinkClient,
    private val evidence: EvidenceText,
    private val assert: ScenarioAssert,
) {
    fun run() {
        val attr = client.requestToChannel(Contracts.CHANNEL, EchoAttrRequest("attr-request"))
            .packetName("EchoAttr")
            .timeout(requestTimeout)
            .await(EchoReply::class.java)
        assert.that(attr.value == "echo:attr-request" && attr.handler == "attr", "RC-A2 request mismatch")
        client.sendToChannel(Contracts.CHANNEL, EchoAttrCommand("attr-send"))
            .packetName("EchoAttr")
            .await()
        evidence.waitForEvidence("Send", "EchoAttr", "attr-send")
        println("scenario RC-A2 passed")
    }
}
