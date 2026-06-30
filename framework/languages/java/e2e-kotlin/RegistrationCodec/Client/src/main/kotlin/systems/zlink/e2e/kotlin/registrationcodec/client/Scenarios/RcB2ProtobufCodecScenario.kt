package systems.zlink.e2e.kotlin.registrationcodec.client.scenarios

import com.google.protobuf.StringValue
import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.client.support.CodecScenarioResult
import systems.zlink.e2e.kotlin.registrationcodec.client.support.EvidenceText
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ScenarioAssert
import systems.zlink.framework.channels.ZLinkClient

class RcB2ProtobufCodecScenario(
    private val client: ZLinkClient,
    private val evidence: EvidenceText,
    private val assert: ScenarioAssert,
) {
    fun run(): CodecScenarioResult {
        val protobufReply = client.requestToChannel(Contracts.CHANNEL, StringValue.of("protobuf-request"))
            .packetName("ProtobufEcho")
            .timeout(requestTimeout)
            .await(StringValue::class.java)
        assert.that(protobufReply.value == "echo:protobuf-request", "RC-B2 request mismatch")
        client.sendToChannel(Contracts.CHANNEL, StringValue.of("protobuf-send"))
            .packetName("ProtobufEcho")
            .await()
        evidence.waitForEvidence("Send", "ProtobufEcho", "protobuf-send")
        println("scenario RC-B2 passed")
        return CodecScenarioResult("RC-B2", "ProtobufEcho", protobufReply.value)
    }
}
