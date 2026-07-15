package systems.zlink.framework.runtime.streams;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.streams.ZLinkStreamCodec;

final class ZLinkStreamSessionContextStateTest {
    @Test
    void closeExecutesTheRuntimeOwnedSessionCloseAction() {
        AtomicInteger closes = new AtomicInteger();
        ZLinkStreamSessionContextState context = new ZLinkStreamSessionContextState(
            "session",
            null,
            RoutingId.from("client-a"),
            null,
            null,
            ZLinkStreamCodec.JSON,
            null,
            null,
            () -> {
                closes.incrementAndGet();
                return CompletableFuture.completedFuture(null);
            });

        context.close().toCompletableFuture().join();

        assertEquals(1, closes.get());
    }
}
