package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.TestSupport;
import java.lang.foreign.MemorySegment;
import java.lang.reflect.Field;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
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

    private static long handleAddress(Spot spot) throws Exception {
        Field handleField = Spot.class.getDeclaredField("handle");
        handleField.setAccessible(true);
        MemorySegment handle = (MemorySegment) handleField.get(spot);
        return handle == null ? 0L : handle.address();
    }
}
