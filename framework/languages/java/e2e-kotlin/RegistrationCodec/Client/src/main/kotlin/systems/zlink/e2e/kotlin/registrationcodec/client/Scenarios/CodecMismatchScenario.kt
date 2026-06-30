package systems.zlink.e2e.kotlin.registrationcodec.client.scenarios

import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoReq
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoRes
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoRes
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoReq
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ScenarioAssert
import systems.zlink.framework.channels.ZLinkClient

class CodecMismatchScenario(
    private val client: ZLinkClient,
    private val assert: ScenarioAssert,
) {
    fun run() {
        val jsonReply = client.requestToChannel(Contracts.CHANNEL, JsonEchoReq("json-after-mismatch"))
            .packetName("JsonEchoReq")
            .timeout(requestTimeout)
            .await(JsonEchoRes::class.java)
        assert.that(jsonReply.value == "echo:json-after-mismatch", "RC-B5 JSON baseline failed")

        try {
            val mismatchReply = client.requestToChannel(Contracts.CHANNEL, PackedEchoReq("msgpack-mismatch"))
                .packetName("PackedEchoReq")
                .timeout(requestTimeout)
                .await(PackedEchoRes::class.java)
            assert.that(mismatchReply.value == "echo:msgpack-mismatch", "RC-B5 fallback reply mismatch")
        } catch (expected: RuntimeException) {
            println("scenario RC-B5 mismatch ended with public error: ${expected.javaClass.simpleName}")
        }

        val secondJson = client.requestToChannel(Contracts.CHANNEL, JsonEchoReq("json-still-ok"))
            .packetName("JsonEchoReq")
            .timeout(requestTimeout)
            .await(JsonEchoRes::class.java)
        assert.that(secondJson.value == "echo:json-still-ok", "RC-B5 JSON traffic did not recover after mismatch")
        println("scenario RC-B5 passed")
    }
}
