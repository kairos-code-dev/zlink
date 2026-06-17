package systems.zlink.framework.runtime.configuration;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;

final class ZLinkCodecRegistrationTest {
    @Test
    void registersSingleCustomSerializerAndRoundTripsPayload() {
        ZLinkCodecRegistration registration = new ZLinkCodecRegistration();
        registration.addSerializer("application/avro", new MarkerSerializer());

        ZLinkMessageSerializer serializer = registration.customSerializer().orElseThrow();
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, new Probe("hello"));

        assertEquals("Probe", encoded.packetName());
        assertEquals("AVRO:hello", new String(encoded.payload().toByteArray(), StandardCharsets.UTF_8));

        Probe decoded = serializer.deserialize(encoded.payload(), Probe.class);
        assertEquals(new Probe("hello"), decoded);
    }

    @Test
    void rejectsBlankContentType() {
        ZLinkCodecRegistration registration = new ZLinkCodecRegistration();

        assertThrows(
            ZLinkConfigurationException.class,
            () -> registration.addSerializer("   ", new MarkerSerializer()));
    }

    @Test
    void ambiguousWhenMultipleCustomSerializersRegistered() {
        ZLinkCodecRegistration registration = new ZLinkCodecRegistration();
        registration.addSerializer("application/avro", new MarkerSerializer());
        registration.addSerializer("application/thrift", new MarkerSerializer());

        assertThrows(ZLinkConfigurationException.class, registration::customSerializer);
    }

    record Probe(String text) {
    }

    static final class MarkerSerializer implements ZLinkMessageSerializer {
        @Override
        public <T> Message serialize(T value) {
            Probe probe = (Probe) value;
            return Message.from(("AVRO:" + probe.text()).getBytes(StandardCharsets.UTF_8));
        }

        @Override
        public <T> T deserialize(Message message, Class<T> type) {
            String text = new String(message.toByteArray(), StandardCharsets.UTF_8);
            String value = text.startsWith("AVRO:") ? text.substring("AVRO:".length()) : text;
            return type.cast(new Probe(value));
        }
    }
}
