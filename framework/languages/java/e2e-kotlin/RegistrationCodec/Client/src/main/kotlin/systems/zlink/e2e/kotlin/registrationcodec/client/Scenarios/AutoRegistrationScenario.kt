package systems.zlink.e2e.kotlin.registrationcodec.client.scenarios

import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.EchoAutoMsg
import systems.zlink.e2e.kotlin.registrationcodec.EchoAutoReq
import systems.zlink.e2e.kotlin.registrationcodec.EchoAutoRes
import systems.zlink.e2e.kotlin.registrationcodec.client.support.EvidenceText
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ScenarioAssert
import systems.zlink.framework.channels.ZLinkClient

class AutoRegistrationScenario(
    private val client: ZLinkClient,
    private val evidence: EvidenceText,
    private val assert: ScenarioAssert,
) {
    fun run() {
        val auto = client.requestToChannel(Contracts.CHANNEL, EchoAutoReq("auto-request"))
            .packetName("EchoAutoReq")
            .timeout(requestTimeout)
            .await(EchoAutoRes::class.java)
        assert.that(auto.value == "echo:auto-request" && auto.handler == "auto", "RC-A1 request mismatch")
        client.sendToChannel(Contracts.CHANNEL, EchoAutoMsg("auto-send"))
            .packetName("EchoAutoMsg")
            .await()
        evidence.waitForEvidence("Send", "EchoAutoMsg", "auto-send")
        println("scenario RC-A1 passed")
    }
}
