package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.AutoHwmProfile;
import systems.zlink.contracts.Context;
import systems.zlink.contracts.RoutingId;
import systems.zlink.contracts.TestSupport;
import java.lang.foreign.MemorySegment;
import java.lang.reflect.Field;
import java.time.Duration;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class SpotNodeLifecycleTest {
    @Test
    void closeCascadesToOwnedSpots() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = new Context()) {
            SpotNode node = new SpotNode(ctx);
            Spot first = node.createSpot();
            Spot second = node.createSpot();

            assertTrue(handleAddress(first) != 0L);
            assertTrue(handleAddress(second) != 0L);

            node.close();

            assertEquals(0L, handleAddress(first));
            assertEquals(0L, handleAddress(second));
            assertDoesNotThrow(first::close);
            assertDoesNotThrow(second::close);
        }
    }

    @Test
    void spotLookupAndOptionFacadesUseCoreEntrypoints() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx)) {
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

    private static long handleAddress(Spot spot) throws Exception {
        Field handleField = Spot.class.getDeclaredField("handle");
        handleField.setAccessible(true);
        MemorySegment handle = (MemorySegment) handleField.get(spot);
        return handle == null ? 0L : handle.address();
    }
}
