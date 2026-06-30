package systems.zlink.e2e.kotlin.registrationcodec.codecrequester

import com.google.protobuf.StringValue
import java.time.Duration
import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoReq
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoRes
import systems.zlink.framework.channels.ZLinkClient

class CodecRequesterProbe(
    private val client: ZLinkClient,
) {
    fun requestJson(): JsonEchoRes =
        client.requestToChannel(Contracts.CHANNEL, JsonEchoReq("json-from-requester"))
            .packetName("JsonEchoReq")
            .timeout(Duration.ofSeconds(5))
            .await(JsonEchoRes::class.java)

    fun requestProtobuf(): CodecMismatchProbeRes =
        try {
            val reply = client.requestToChannel(Contracts.CHANNEL, StringValue.of("protobuf-mismatch"))
                .packetName("ProtobufEcho")
                .timeout(Duration.ofSeconds(2))
                .await(StringValue::class.java)
            CodecMismatchProbeRes(error = false, errorType = null, value = reply.value)
        } catch (error: RuntimeException) {
            CodecMismatchProbeRes(error = true, errorType = error.javaClass.simpleName, value = null)
        }
}

data class CodecMismatchProbeRes(
    val error: Boolean,
    val errorType: String?,
    val value: String?,
)
