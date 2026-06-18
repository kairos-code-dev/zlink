package systems.zlink.framework.codecs.msgpack;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;
import systems.zlink.framework.ZLinkMessageSerializer;
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

    record ProfileReply(String id) {
    }
}
