package systems.zlink.framework.runtime.messaging;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRefSnapshot;

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

    @Test
    void preservesFrameworkActorReferences() {
        ZLinkJsonMessageSerializer serializer = new ZLinkJsonMessageSerializer();
        ActorRefSnapshot expected = new ActorRefSnapshot(
            RoutingId.from(new byte[] {0, 65, 66}),
            "courier-a",
            7L);

        ActorRefSnapshot actual = serializer.deserialize(
            serializer.serialize(expected),
            ActorRefSnapshot.class);

        assertEquals(expected, actual);
    }

    record ProfileReply(String value) {
    }

    record CourierActorFound(String courierId, ActorRefWire actor) {
    }

    record ActorRefWire(String nodeRid, String actorId, long generation) {
    }
}
