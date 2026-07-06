package systems.zlink.framework.runtime.messaging;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

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

    @Test
    void preservesNullRecordFields() {
        ZLinkJsonMessageSerializer serializer = new ZLinkJsonMessageSerializer();

        CourierActorFound reply = serializer.deserialize(
            serializer.serialize(new CourierActorFound("courier-a", null)),
            CourierActorFound.class);

        assertEquals("courier-a", reply.courierId());
        assertNull(reply.actor());
    }

    record ProfileReply(String value) {
    }

    record CourierActorFound(String courierId, ActorRefWire actor) {
    }

    record ActorRefWire(String nodeRid, String actorId, long generation) {
    }
}
