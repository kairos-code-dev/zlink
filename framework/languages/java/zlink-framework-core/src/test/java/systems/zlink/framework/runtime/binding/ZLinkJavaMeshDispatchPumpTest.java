package systems.zlink.framework.runtime.binding;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;
import java.util.function.IntUnaryOperator;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.runtime.internal.backend.ZLinkMeshDispatchRecord;

class ZLinkJavaMeshDispatchPumpTest {
    @Test
    void callbackOnlySchedulesAndResidueIsDrainedSerially() throws Exception {
        RecordingSource source = new RecordingSource();
        CountDownLatch drained = new CountDownLatch(2);
        ExecutorService executor = Executors.newSingleThreadExecutor();

        try (ZLinkJavaMeshDispatchPump pump =
                 new ZLinkJavaMeshDispatchPump(source, ignored -> drained.countDown(), executor)) {
            assertEquals(3, source.readyHandler.applyAsInt(3));
            assertTrue(drained.await(2, TimeUnit.SECONDS));
            assertEquals(List.of(3, 3), source.domains);
            assertEquals(1, source.maxConcurrentDrains);
        }

        assertTrue(source.closed);
    }

    private static final class RecordingSource implements ZLinkJavaMeshDispatchPump.Source {
        private final List<Integer> domains = new ArrayList<>();
        private IntUnaryOperator readyHandler;
        private int activeDrains;
        private int maxConcurrentDrains;
        private boolean first = true;
        private boolean closed;

        @Override
        public void setReadyHandler(IntUnaryOperator handler) {
            readyHandler = handler;
        }

        @Override
        public synchronized boolean drain(
            int readyDomains,
            Consumer<ZLinkMeshDispatchRecord> receiver) {
            activeDrains++;
            maxConcurrentDrains = Math.max(maxConcurrentDrains, activeDrains);
            try {
                domains.add(readyDomains);
                receiver.accept(null);
                if (first) {
                    first = false;
                    return true;
                }
                return false;
            } finally {
                activeDrains--;
            }
        }

        @Override
        public void close() {
            closed = true;
        }
    }
}
