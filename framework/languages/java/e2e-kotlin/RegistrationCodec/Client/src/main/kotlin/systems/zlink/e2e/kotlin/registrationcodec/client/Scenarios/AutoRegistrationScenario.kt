package systems.zlink.e2e.kotlin.registrationcodec.client.scenarios

import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.EchoAutoCommand
import systems.zlink.e2e.kotlin.registrationcodec.EchoAutoRequest
import systems.zlink.e2e.kotlin.registrationcodec.EchoReply
import systems.zlink.e2e.kotlin.registrationcodec.client.support.EvidenceText
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ScenarioAssert
import systems.zlink.framework.channels.ZLinkClient

class AutoRegistrationScenario(
    private val client: ZLinkClient,
    private val evidence: EvidenceText,
    private val assert: ScenarioAssert,
) {
    fun run() {
        val auto = client.requestToChannel(Contracts.CHANNEL, EchoAutoRequest("auto-request"))
            .packetName("EchoAuto")
            .timeout(requestTimeout)
            .await(EchoReply::class.java)
        assert.that(auto.value == "echo:auto-request" && auto.handler == "auto", "RC-A1 request mismatch")
        client.sendToChannel(Contracts.CHANNEL, EchoAutoCommand("auto-send"))
            .packetName("EchoAuto")
            .await()
        evidence.waitForEvidence("Send", "EchoAuto", "auto-send")
        println("scenario RC-A1 passed")
    }
}
