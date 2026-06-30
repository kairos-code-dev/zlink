package systems.zlink.e2e.kotlin.registrationcodec.client.scenarios

import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.EchoAttrMsg
import systems.zlink.e2e.kotlin.registrationcodec.EchoAttrReq
import systems.zlink.e2e.kotlin.registrationcodec.EchoAttrRes
import systems.zlink.e2e.kotlin.registrationcodec.client.support.EvidenceText
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ScenarioAssert
import systems.zlink.framework.channels.ZLinkClient

class AttributeRegistrationScenario(
    private val client: ZLinkClient,
    private val evidence: EvidenceText,
    private val assert: ScenarioAssert,
) {
    fun run() {
        val attr = client.requestToChannel(Contracts.CHANNEL, EchoAttrReq("attr-request"))
            .packetName("EchoAttrReq")
            .timeout(requestTimeout)
            .await(EchoAttrRes::class.java)
        assert.that(attr.value == "echo:attr-request" && attr.handler == "attr", "RC-A2 request mismatch")
        client.sendToChannel(Contracts.CHANNEL, EchoAttrMsg("attr-send"))
            .packetName("EchoAttrMsg")
            .await()
        evidence.waitForEvidence("Send", "EchoAttrMsg", "attr-send")
        println("scenario RC-A2 passed")
    }
}
