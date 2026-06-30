package systems.zlink.e2e.kotlin.registrationcodec.client.scenarios

import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoMsg
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoReq
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoRes
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
        val jsonReply = client.requestToChannel(Contracts.CHANNEL, JsonEchoReq("json-request"))
            .packetName("JsonEchoReq")
            .timeout(requestTimeout)
            .await(JsonEchoRes::class.java)
        assert.that(jsonReply.value == "echo:json-request" && jsonReply.handler == "json", "RC-B1 request mismatch")
        client.sendToChannel(Contracts.CHANNEL, JsonEchoMsg("json-send"))
            .packetName("JsonEchoMsg")
            .await()
        evidence.waitForEvidence("Send", "JsonEchoMsg", "json-send")
        println("scenario RC-B1 passed")
        return CodecScenarioResult("RC-B1", "JsonEchoReq", jsonReply.value)
    }
}
