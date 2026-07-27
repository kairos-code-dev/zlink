package systems.zlink.framework.runtime;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.time.Instant;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.runtime.internal.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkSocketEvent;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;

final class MonitoringRunnerTest {
    @Test
    void handlerFailure_recordsDiagnostic_withoutStopping() {
        ZLinkRuntimeEventDispatcher dispatcher = new ZLinkRuntimeEventDispatcher();
        AtomicInteger successfulCalls = new AtomicInteger();
        dispatcher.register(ZLinkSocketEvent.class, event -> {
            throw new IllegalStateException("handler failed");
        });
        dispatcher.register(ZLinkSocketEvent.class, event -> {
            successfulCalls.incrementAndGet();
        });

        dispatcher.publish(event());
        dispatcher.publish(event());

        assertEquals(2, dispatcher.handlerFailureCount());
        assertEquals(2, successfulCalls.get());
    }

    private static ZLinkSocketEvent event() {
        return new ZLinkSocketEvent(
            "profile",
            Instant.now(),
            ZLinkSocketEventKind.CONNECTED,
            Optional.empty(),
            "",
            "",
            Optional.empty());
    }
}
