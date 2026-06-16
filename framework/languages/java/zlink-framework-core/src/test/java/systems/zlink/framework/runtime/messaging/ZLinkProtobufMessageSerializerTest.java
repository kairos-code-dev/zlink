package systems.zlink.framework.runtime.messaging;

import static org.junit.jupiter.api.Assertions.assertEquals;

import com.google.protobuf.StringValue;
import org.junit.jupiter.api.Test;

final class ZLinkProtobufMessageSerializerTest {
    @Test
    void protobufSerializerUsesMessageLiteBytesAndKeepsJsonFallback() {
        ZLinkProtobufMessageSerializer serializer =
            new ZLinkProtobufMessageSerializer(new ZLinkJsonMessageSerializer());

        StringValue original = StringValue.of("profile:42");
        StringValue decoded = serializer.deserialize(
            serializer.serialize(original),
            StringValue.class);
        assertEquals(original, decoded);

        ProfileReply fallback = serializer.deserialize(
            serializer.serialize(new ProfileReply("json:42")),
            ProfileReply.class);
        assertEquals(new ProfileReply("json:42"), fallback);
    }

    record ProfileReply(String id) {
    }
}
