package systems.zlink.e2e.kotlin.registrationcodec.client.scenarios

import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.EchoManualMsg
import systems.zlink.e2e.kotlin.registrationcodec.EchoManualReq
import systems.zlink.e2e.kotlin.registrationcodec.EchoManualRes
import systems.zlink.e2e.kotlin.registrationcodec.client.support.EvidenceText
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ScenarioAssert
import systems.zlink.framework.channels.ZLinkClient

class ManualRegistrationScenario(
    private val client: ZLinkClient,
    private val evidence: EvidenceText,
    private val assert: ScenarioAssert,
) {
    fun run() {
        val manual = client.requestToChannel(Contracts.CHANNEL, EchoManualReq("manual-request"))
            .packetName("EchoManualReq")
            .timeout(requestTimeout)
            .await(EchoManualRes::class.java)
        assert.that(manual.value == "echo:manual-request" && manual.handler == "manual", "RC-A3 request mismatch")
        client.sendToChannel(Contracts.CHANNEL, EchoManualMsg("manual-send"))
            .packetName("EchoManualMsg")
            .await()
        evidence.waitForEvidence("Send", "EchoManualMsg", "manual-send")
        println("scenario RC-A3 passed")
    }
}
