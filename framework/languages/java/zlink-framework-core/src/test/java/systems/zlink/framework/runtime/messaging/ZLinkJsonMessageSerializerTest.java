package systems.zlink.framework.runtime.messaging;

import static org.junit.jupiter.api.Assertions.assertEquals;

import org.junit.jupiter.api.Test;

final class ZLinkJsonMessageSerializerTest {
    @Test
    void serializesAndDeserializesRecordContracts() {
        ZLinkJsonMessageSerializer serializer = new ZLinkJsonMessageSerializer();

        ProfileReply reply = serializer.deserialize(
            serializer.serialize(new ProfileReply("profile:42")),
            ProfileReply.class);

        assertEquals(new ProfileReply("profile:42"), reply);
    }

    record ProfileReply(String value) {
    }
}
