package systems.zlink.e2e.kotlin.registrationcodec.codecrequester

import com.google.protobuf.StringValue
import java.time.Duration
import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.EchoReply
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoRequest
import systems.zlink.framework.channels.ZLinkClient

class CodecRequesterProbe(
    private val client: ZLinkClient,
) {
    fun requestJson(): EchoReply =
        client.requestToChannel(Contracts.CHANNEL, JsonEchoRequest("json-from-requester"))
            .packetName("JsonEcho")
            .timeout(Duration.ofSeconds(5))
            .await(EchoReply::class.java)

    fun requestProtobuf(): CodecMismatchProbeReply =
        try {
            val reply = client.requestToChannel(Contracts.CHANNEL, StringValue.of("protobuf-mismatch"))
                .packetName("ProtobufEcho")
                .timeout(Duration.ofSeconds(2))
                .await(StringValue::class.java)
            CodecMismatchProbeReply(error = false, errorType = null, value = reply.value)
        } catch (error: RuntimeException) {
            CodecMismatchProbeReply(error = true, errorType = error.javaClass.simpleName, value = null)
        }
}

data class CodecMismatchProbeReply(
    val error: Boolean,
    val errorType: String?,
    val value: String?,
)
