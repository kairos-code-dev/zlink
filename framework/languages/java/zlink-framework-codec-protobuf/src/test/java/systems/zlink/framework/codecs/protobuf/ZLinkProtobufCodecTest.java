package systems.zlink.framework.codecs.protobuf;

import static org.junit.jupiter.api.Assertions.assertEquals;

import com.google.protobuf.StringValue;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.runtime.configuration.ZLinkCodecRegistration;
import systems.zlink.framework.runtime.messaging.ZLinkJsonMessageSerializer;

final class ZLinkProtobufCodecTest {
    @Test
    void protobufExtensionUsesMessageLiteBytesAndKeepsJsonFallback() {
        ZLinkCodecRegistration registration = new ZLinkCodecRegistration();
        registration.use(ZLinkProtobufCodec.defaultCodec());
        ZLinkMessageSerializer serializer =
            registration.serializerWithFallback(new ZLinkJsonMessageSerializer());

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
