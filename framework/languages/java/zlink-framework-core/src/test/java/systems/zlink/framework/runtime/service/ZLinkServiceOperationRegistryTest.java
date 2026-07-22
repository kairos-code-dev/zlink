package systems.zlink.framework.runtime.service;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.concurrent.CompletionException;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import org.junit.jupiter.api.Test;

final class ZLinkServiceOperationRegistryTest {
    @Test
    void firstTerminalPathWinsAndDetachesDeadline() {
        ScheduledExecutorService scheduler = Executors.newSingleThreadScheduledExecutor();
        try (ZLinkServiceOperationRegistry registry = new ZLinkServiceOperationRegistry(scheduler)) {
            ZLinkServiceOperationRegistry.Operation<String> operation =
                registry.register(Duration.ofSeconds(1));
            assertTrue(registry.complete(operation.id(), "reply"));
            assertFalse(registry.completeExceptionally(
                operation.id(), new TimeoutException("late timeout")));
            assertEquals("reply", operation.completion().join());
            assertEquals(0, registry.pendingCount());
        } finally {
            scheduler.shutdownNow();
        }
    }

    @Test
    void timeoutCompletesExactlyOnce() throws Exception {
        ScheduledExecutorService scheduler = Executors.newSingleThreadScheduledExecutor();
        try (ZLinkServiceOperationRegistry registry = new ZLinkServiceOperationRegistry(scheduler)) {
            ZLinkServiceOperationRegistry.Operation<String> operation =
                registry.register(Duration.ofMillis(10));
            CompletionException failure = assertThrows(
                CompletionException.class,
                () -> operation.completion().orTimeout(1, TimeUnit.SECONDS).join());
            assertTrue(failure.getCause() instanceof TimeoutException);
            assertFalse(registry.complete(operation.id(), "late reply"));
        } finally {
            scheduler.shutdownNow();
        }
    }
}
