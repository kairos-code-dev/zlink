package systems.zlink.framework.kotlin

import java.nio.charset.StandardCharsets
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.ZLinkMessageSerializer
import systems.zlink.framework.messaging.ZLinkMessage

final class KotlinMessageCodecBoundaryTest {
    @Test
    fun kotlinHandlersUseFrameworkMessageDecodeWithCustomSerializer() {
        val serializer = KotlinBoundarySerializer()
        val encoded = serializer.serialize(KotlinBoundaryPayload("custom"))

        encoded.use {
            val message = ZLinkMessage.fromMessage(it, serializer)
            assertEquals(KotlinBoundaryPayload("custom"), message.decode<KotlinBoundaryPayload>())
        }
    }

    @Test
    fun kotlinMessageOfKeepsTypedValueForFrameworkMessageDecode() {
        val message = messageOf(KotlinBoundaryPayload("typed"))

        assertEquals(KotlinBoundaryPayload("typed"), message.decode<KotlinBoundaryPayload>())
    }
}

data class KotlinBoundaryPayload(val value: String)

private class KotlinBoundarySerializer : ZLinkMessageSerializer {
    override fun <T : Any?> serialize(value: T): Message =
        when (value) {
            is Message -> Message.from(value)
            is KotlinBoundaryPayload -> Message.from(
                value.value.toByteArray(StandardCharsets.UTF_8),
            )
            else -> Message.from(value.toString().toByteArray(StandardCharsets.UTF_8))
        }

    override fun <T : Any?> deserialize(message: Message, type: Class<T>): T {
        if (type == Message::class.java) {
            return type.cast(Message.from(message))
        }
        if (type == KotlinBoundaryPayload::class.java) {
            return type.cast(KotlinBoundaryPayload(message.toUtf8String()))
        }
        throw IllegalArgumentException("unsupported message type: ${type.name}")
    }
}
