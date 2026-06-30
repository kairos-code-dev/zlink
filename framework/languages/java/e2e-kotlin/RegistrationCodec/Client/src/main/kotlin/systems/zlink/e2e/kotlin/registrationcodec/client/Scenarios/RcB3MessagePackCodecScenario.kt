package systems.zlink.e2e.kotlin.registrationcodec.client.scenarios

import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoCommand
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoReply
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoRequest
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
        val packedReply = client.requestToChannel(Contracts.CHANNEL, PackedEchoRequest("msgpack-request"))
            .packetName("MsgpackEcho")
            .timeout(requestTimeout)
            .await(PackedEchoReply::class.java)
        assert.that(packedReply.value == "echo:msgpack-request", "RC-B3 request mismatch")
        client.sendToChannel(Contracts.CHANNEL, PackedEchoCommand("msgpack-send"))
            .packetName("MsgpackEcho")
            .await()
        evidence.waitForEvidence("Send", "MsgpackEcho", "msgpack-send")
        println("scenario RC-B3 passed")
        return CodecScenarioResult("RC-B3", "MsgpackEcho", packedReply.value)
    }
}
