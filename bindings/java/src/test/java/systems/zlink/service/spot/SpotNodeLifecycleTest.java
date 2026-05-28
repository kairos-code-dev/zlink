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

            assertDoesNotThrow(first::routingId);
            assertDoesNotThrow(second::routingId);

            node.close();

            assertThrows(IllegalStateException.class, first::routingId);
            assertThrows(IllegalStateException.class, second::routingId);
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
            node.routerHwm(128);
            assertEquals(128, node.routerHwm());
            node.pubsubHwmProfile(AutoHwmProfile.LOW_LATENCY);
            assertEquals(AutoHwmProfile.LOW_LATENCY, node.pubsubHwmProfile());
            node.pubsubHwm(64);
            assertEquals(64, node.pubsubHwm());

            spot.requestTimeout(Duration.ofMillis(123));
            assertEquals(Duration.ofMillis(123), spot.requestTimeout());
        }
    }

}
