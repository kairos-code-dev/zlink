package systems.zlink.framework.runtime.streams;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.streams.ZLinkStreamCodec;

final class ZLinkStreamSessionContextStateTest {
    @Test
    void replyHeaderCanBeClaimedOnlyOnce() {
        ZLinkStreamSessionContextState context = context(new AtomicInteger());
        ZLinkStreamHeader request = new ZLinkStreamHeader(
            "Request",
            Map.of(),
            Optional.of(7L));

        assertTrue(context.claimReplyHeader(request));
        assertFalse(context.claimReplyHeader(request));
    }

    @Test
    void concurrentReplyClaimsHaveExactlyOneWinner() throws Exception {
        for (int iteration = 0; iteration < 100; iteration++) {
            ZLinkStreamSessionContextState context = context(new AtomicInteger());
            ZLinkStreamHeader request = new ZLinkStreamHeader(
                "Request",
                Map.of(),
                Optional.of((long) iteration + 1L));
            CountDownLatch start = new CountDownLatch(1);
            AtomicInteger winners = new AtomicInteger();
            Thread first = Thread.ofVirtual().start(() -> claim(context, request, start, winners));
            Thread second = Thread.ofVirtual().start(() -> claim(context, request, start, winners));

            start.countDown();
            first.join();
            second.join();

            assertEquals(1, winners.get());
            assertFalse(context.claimReplyHeader(request));
        }
    }

    @Test
    void closeExecutesTheRuntimeOwnedSessionCloseAction() {
        AtomicInteger closes = new AtomicInteger();
        ZLinkStreamSessionContextState context = context(closes);

        context.close().toCompletableFuture().join();

        assertEquals(1, closes.get());
    }

    private static ZLinkStreamSessionContextState context(AtomicInteger closes) {
        return new ZLinkStreamSessionContextState(
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
    }

    private static void claim(
        ZLinkStreamSessionContextState context,
        ZLinkStreamHeader request,
        CountDownLatch start,
        AtomicInteger winners) {
        try {
            start.await();
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new AssertionError(error);
        }
        if (context.claimReplyHeader(request)) {
            winners.incrementAndGet();
        }
    }
}
