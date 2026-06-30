package systems.zlink.e2e.kotlin.registrationcodec.client.scenarios

import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.EchoReply
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoRequest
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoReply
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoRequest
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ScenarioAssert
import systems.zlink.framework.channels.ZLinkClient

class CodecMismatchScenario(
    private val client: ZLinkClient,
    private val assert: ScenarioAssert,
) {
    fun run() {
        val jsonReply = client.requestToChannel(Contracts.CHANNEL, JsonEchoRequest("json-after-mismatch"))
            .packetName("JsonEcho")
            .timeout(requestTimeout)
            .await(EchoReply::class.java)
        assert.that(jsonReply.value == "echo:json-after-mismatch", "RC-B5 JSON baseline failed")

        try {
            val mismatchReply = client.requestToChannel(Contracts.CHANNEL, PackedEchoRequest("msgpack-mismatch"))
                .packetName("MsgpackEcho")
                .timeout(requestTimeout)
                .await(PackedEchoReply::class.java)
            assert.that(mismatchReply.value == "echo:msgpack-mismatch", "RC-B5 fallback reply mismatch")
        } catch (expected: RuntimeException) {
            println("scenario RC-B5 mismatch ended with public error: ${expected.javaClass.simpleName}")
        }

        val secondJson = client.requestToChannel(Contracts.CHANNEL, JsonEchoRequest("json-still-ok"))
            .packetName("JsonEcho")
            .timeout(requestTimeout)
            .await(EchoReply::class.java)
        assert.that(secondJson.value == "echo:json-still-ok", "RC-B5 JSON traffic did not recover after mismatch")
        println("scenario RC-B5 passed")
    }
}
