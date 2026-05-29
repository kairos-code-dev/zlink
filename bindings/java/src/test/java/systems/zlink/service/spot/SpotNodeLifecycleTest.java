package systems.zlink.contracts.service.spot;

import systems.zlink.TestSupport;
import systems.zlink.contracts.sockets.AutoHwmProfile;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.core.RoutingId;
import java.time.Duration;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class SpotNodeLifecycleTest {
    @Test
    void closeCascadesToOwnedSpots() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext()) {
            SpotNode node = ctx.createSpotNode();
            Spot first = node.createSpot();
            Spot second = node.createSpot();

            assertDoesNotThrow(first::getRoutingId);
            assertDoesNotThrow(second::getRoutingId);

            node.close();

            assertThrows(IllegalStateException.class, first::getRoutingId);
            assertThrows(IllegalStateException.class, second::getRoutingId);
            assertDoesNotThrow(first::close);
            assertDoesNotThrow(second::close);
        }
    }

    @Test
    void spotLookupAndOptionFacadesUseCoreEntrypoints() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             SpotNode node = ctx.createSpotNode()) {
            Spot entry = node.entrySpot();
            assertNotNull(entry);

            Spot spot = node.createSpot();
            RoutingId rid = RoutingId.from(new byte[] {1, 2, 3, 4});
            spot.setRoutingId(rid);
            Spot lookup = node.spotLookup(rid).orElseThrow();
            assertNotNull(lookup);
            assertTrue(node.spotLookup(RoutingId.from(new byte[] {9, 9}))
                .isEmpty());

            node.routerHwmProfile(AutoHwmProfile.BALANCED);
            assertEquals(AutoHwmProfile.BALANCED, node.routerHwmProfile());
            node.routerHighWaterMark(128);
            assertEquals(128, node.routerHighWaterMark());
            node.pubSubHwmProfile(AutoHwmProfile.LOW_LATENCY);
            assertEquals(AutoHwmProfile.LOW_LATENCY, node.pubSubHwmProfile());
            node.pubSubHighWaterMark(64);
            assertEquals(64, node.pubSubHighWaterMark());

            spot.requestTimeout(Duration.ofMillis(123));
            assertEquals(Duration.ofMillis(123), spot.requestTimeout());
        }
    }

}
