package systems.zlink.e2e.kotlin.registrationcodec.client.scenarios

import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.EchoReply
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoCommand
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoRequest
import systems.zlink.e2e.kotlin.registrationcodec.client.support.CodecScenarioResult
import systems.zlink.e2e.kotlin.registrationcodec.client.support.EvidenceText
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ScenarioAssert
import systems.zlink.framework.channels.ZLinkClient

class RcB1JsonCodecScenario(
    private val client: ZLinkClient,
    private val evidence: EvidenceText,
    private val assert: ScenarioAssert,
) {
    fun run(): CodecScenarioResult {
        val jsonReply = client.requestToChannel(Contracts.CHANNEL, JsonEchoRequest("json-request"))
            .packetName("JsonEcho")
            .timeout(requestTimeout)
            .await(EchoReply::class.java)
        assert.that(jsonReply.value == "echo:json-request" && jsonReply.handler == "json", "RC-B1 request mismatch")
        client.sendToChannel(Contracts.CHANNEL, JsonEchoCommand("json-send"))
            .packetName("JsonEcho")
            .await()
        evidence.waitForEvidence("Send", "JsonEcho", "json-send")
        println("scenario RC-B1 passed")
        return CodecScenarioResult("RC-B1", "JsonEcho", jsonReply.value)
    }
}
