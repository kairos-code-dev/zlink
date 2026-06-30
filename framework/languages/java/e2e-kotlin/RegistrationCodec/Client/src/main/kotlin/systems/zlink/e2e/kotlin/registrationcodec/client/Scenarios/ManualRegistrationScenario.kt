package systems.zlink.e2e.kotlin.registrationcodec.client.scenarios

import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.EchoManualCommand
import systems.zlink.e2e.kotlin.registrationcodec.EchoManualRequest
import systems.zlink.e2e.kotlin.registrationcodec.EchoReply
import systems.zlink.e2e.kotlin.registrationcodec.client.support.EvidenceText
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ScenarioAssert
import systems.zlink.framework.channels.ZLinkClient

class ManualRegistrationScenario(
    private val client: ZLinkClient,
    private val evidence: EvidenceText,
    private val assert: ScenarioAssert,
) {
    fun run() {
        val manual = client.requestToChannel(Contracts.CHANNEL, EchoManualRequest("manual-request"))
            .packetName("EchoManual")
            .timeout(requestTimeout)
            .await(EchoReply::class.java)
        assert.that(manual.value == "echo:manual-request" && manual.handler == "manual", "RC-A3 request mismatch")
        client.sendToChannel(Contracts.CHANNEL, EchoManualCommand("manual-send"))
            .packetName("EchoManual")
            .await()
        evidence.waitForEvidence("Send", "EchoManual", "manual-send")
        println("scenario RC-A3 passed")
    }
}
