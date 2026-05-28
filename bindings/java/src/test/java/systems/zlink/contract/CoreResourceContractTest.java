package systems.zlink.contract;

import systems.zlink.TestSupport;
import systems.zlink.contracts.core.AtomicCounter;
import systems.zlink.contracts.core.Stopwatch;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.core.ZlinkThread;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class CoreResourceContractTest {
    @Test
    public void coreNativeResourcesUseContractInterfacesAndFactories() {
        TestSupport.assumeNative();

        assertTrue(AtomicCounter.class.isInterface());
        assertTrue(Stopwatch.class.isInterface());
        assertTrue(ZlinkThread.class.isInterface());
        assertEquals(0, AtomicCounter.class.getConstructors().length);
        assertEquals(0, Stopwatch.class.getConstructors().length);
        assertEquals(0, ZlinkThread.class.getConstructors().length);

        try (AtomicCounter counter = Zlink.createAtomicCounter()) {
            counter.set(7);
            assertEquals(7, counter.value());
            counter.increment();
            assertEquals(8, counter.value());
            counter.decrement();
            assertEquals(7, counter.value());
        }

        try (Stopwatch stopwatch = Zlink.createStopwatch()) {
            assertFalse(stopwatch.intermediate().isNegative());
            assertFalse(stopwatch.stop().isNegative());
        }

        AtomicInteger ran = new AtomicInteger();
        try (ZlinkThread thread = Zlink.createThread(ran::incrementAndGet)) {
            thread.join();
        }
        assertEquals(1, ran.get());

        try (AtomicCounter counter = Zlink.createAtomicCounter();
             Stopwatch stopwatch = Zlink.createStopwatch()) {
            assertTrue(AtomicCounter.class.isInstance(counter));
            assertTrue(Stopwatch.class.isInstance(stopwatch));
        }
    }
}
