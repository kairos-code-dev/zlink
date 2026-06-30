package systems.zlink.e2e.kotlin.registrationcodec.client.scenarios

import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoMsg
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoRes
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoReq
import systems.zlink.e2e.kotlin.registrationcodec.client.support.CodecScenarioResult
import systems.zlink.e2e.kotlin.registrationcodec.client.support.EvidenceText
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ScenarioAssert
import systems.zlink.framework.channels.ZLinkClient

class RcB3MessagePackCodecScenario(
    private val client: ZLinkClient,
    private val evidence: EvidenceText,
    private val assert: ScenarioAssert,
) {
    fun run(): CodecScenarioResult {
        val packedReply = client.requestToChannel(Contracts.CHANNEL, PackedEchoReq("msgpack-request"))
            .packetName("PackedEchoReq")
            .timeout(requestTimeout)
            .await(PackedEchoRes::class.java)
        assert.that(packedReply.value == "echo:msgpack-request", "RC-B3 request mismatch")
        client.sendToChannel(Contracts.CHANNEL, PackedEchoMsg("msgpack-send"))
            .packetName("PackedEchoMsg")
            .await()
        evidence.waitForEvidence("Send", "PackedEchoMsg", "msgpack-send")
        println("scenario RC-B3 passed")
        return CodecScenarioResult("RC-B3", "PackedEchoReq", packedReply.value)
    }
}
