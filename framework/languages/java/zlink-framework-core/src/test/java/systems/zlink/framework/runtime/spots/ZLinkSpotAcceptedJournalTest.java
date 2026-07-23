package systems.zlink.framework.runtime.spots;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.List;
import java.util.Optional;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.runtime.backend.ZLinkBackendReceived;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestResult;

final class ZLinkSpotAcceptedJournalTest {
    @Test
    void acceptedRouteRecordPreservesRelayIdentityMetadataAndParts() {
        ZLinkBackendReceived received = new ZLinkBackendReceived(
            ZLinkBackendRequestResult.OK,
            Optional.of(RoutingId.fromHex("01")),
            Optional.of(RoutingId.fromHex("02")),
            Optional.of(17L),
            new byte[] {3, 4},
            List.of(Message.from(new byte[] {5}), Message.from(new byte[] {6, 7})),
            null,
            () -> { });

        ZLinkSpotAcceptedJournal.Record record =
            ZLinkSpotAcceptedJournal.decode(
                ZLinkSpotAcceptedJournal.encode(received));

        assertEquals(received.result(), record.result());
        assertEquals(received.routingId(), record.routingId());
        assertEquals(received.spotRid(), record.spotRid());
        assertEquals(received.requestSeq(), record.requestSequence());
        assertArrayEquals(new byte[] {3, 4}, record.applicationMetadata());
        assertArrayEquals(new byte[] {5}, record.parts().get(0));
        assertArrayEquals(new byte[] {6, 7}, record.parts().get(1));
        received.close();
    }
}
