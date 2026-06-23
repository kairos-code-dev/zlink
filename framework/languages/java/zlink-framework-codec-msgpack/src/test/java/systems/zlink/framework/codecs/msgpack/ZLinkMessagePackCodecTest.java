package systems.zlink.framework.codecs.msgpack;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.google.protobuf.StringValue;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;
import systems.zlink.framework.runtime.configuration.ZLinkCodecRegistration;
import systems.zlink.framework.runtime.messaging.ZLinkJsonMessageSerializer;

final class ZLinkMessagePackCodecTest {
    @Test
    void messagePackExtensionKeepsTypedPayloadRoundTrip() {
        ZLinkCodecRegistration registration = new ZLinkCodecRegistration();
        registration.use(ZLinkMessagePackCodec.defaultCodec());
        ZLinkMessageSerializer serializer =
            registration.serializerWithFallback(new ZLinkJsonMessageSerializer());

        assertTrue(registration.serializers().containsKey("application/x-msgpack"));
        ProfileReply decoded = serializer.deserialize(
            serializer.serialize(new ProfileReply("msgpack:42")),
            ProfileReply.class);
        assertEquals(new ProfileReply("msgpack:42"), decoded);
    }

    @Test
    void typedMessagePackExtensionCanCoexistWithJsonFallbackAndProtobuf() {
        ZLinkCodecRegistration registration = new ZLinkCodecRegistration();
        registration.use(ZLinkProtobufCodec.defaultCodec());
        registration.use(ZLinkMessagePackCodec.forPayloadTypes(PackedProfileReply.class::equals));
        ZLinkMessageSerializer serializer =
            registration.serializerWithFallback(new ZLinkJsonMessageSerializer());

        ProfileReply json = serializer.deserialize(
            serializer.serialize(new ProfileReply("json:42")),
            ProfileReply.class);
        assertEquals(new ProfileReply("json:42"), json);

        StringValue protobuf = serializer.deserialize(
            serializer.serialize(StringValue.of("protobuf:42")),
            StringValue.class);
        assertEquals(StringValue.of("protobuf:42"), protobuf);

        PackedProfileReply msgpack = serializer.deserialize(
            serializer.serialize(new PackedProfileReply("msgpack:42")),
            PackedProfileReply.class);
        assertEquals(new PackedProfileReply("msgpack:42"), msgpack);
    }

    record ProfileReply(String id) {
    }

    record PackedProfileReply(String id) {
    }
}
