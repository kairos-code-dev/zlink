package systems.zlink.framework.runtime.internal.monitoring;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Flow;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.CopyOnWriteArrayList;
import org.junit.jupiter.api.Test;

final class ZLinkStatusPublisherTest {
    @Test
    void preservedMilestoneIsDeliveredBeforeLaterTerminalSnapshot()
        throws Exception {
        AtomicInteger state = new AtomicInteger();
        Flow.Publisher<Integer> publisher = ZLinkStatusPublisher.create(
            state::get,
            value -> value,
            4,
            value -> value == 3,
            value -> value == 2);
        CopyOnWriteArrayList<Integer> received =
            new CopyOnWriteArrayList<>();
        CompletableFuture<Flow.Subscription> subscribed =
            new CompletableFuture<>();
        CompletableFuture<Void> completed = new CompletableFuture<>();
        publisher.subscribe(new Flow.Subscriber<>() {
            @Override public void onSubscribe(Flow.Subscription subscription) {
                subscribed.complete(subscription);
            }
            @Override public void onNext(Integer item) { received.add(item); }
            @Override public void onError(Throwable failure) {
                completed.completeExceptionally(failure);
            }
            @Override public void onComplete() { completed.complete(null); }
        });
        Flow.Subscription subscription = subscribed.get(1, TimeUnit.SECONDS);
        subscription.request(1);
        long firstDeadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(1);
        while (received.isEmpty() && System.nanoTime() < firstDeadline) {
            Thread.sleep(1);
        }
        state.set(2);
        Thread.sleep(60);
        state.set(3);
        Thread.sleep(60);
        subscription.request(2);

        completed.get(1, TimeUnit.SECONDS);
        assertEquals(java.util.List.of(0, 2, 3), received);
    }

    @Test
    void slowObserverDoesNotDelayAnotherObserverAndTerminalIsDelivered()
        throws Exception {
        AtomicInteger state = new AtomicInteger();
        Flow.Publisher<Integer> publisher = ZLinkStatusPublisher.create(
            state::get,
            value -> value,
            4,
            value -> value == 2);
        CountDownLatch slowEntered = new CountDownLatch(1);
        CountDownLatch releaseSlow = new CountDownLatch(1);
        CompletableFuture<Integer> fastTerminal = new CompletableFuture<>();
        CompletableFuture<Void> fastCompleted = new CompletableFuture<>();

        publisher.subscribe(subscriber(value -> {
            slowEntered.countDown();
            try {
                releaseSlow.await(2, TimeUnit.SECONDS);
            } catch (InterruptedException failure) {
                Thread.currentThread().interrupt();
            }
        }, new CompletableFuture<>()));
        publisher.subscribe(subscriber(value -> {
            if (value == 2) {
                fastTerminal.complete(value);
            }
        }, fastCompleted));

        assertTrue(slowEntered.await(1, TimeUnit.SECONDS));
        state.set(2);
        assertEquals(2, fastTerminal.get(1, TimeUnit.SECONDS));
        fastCompleted.get(1, TimeUnit.SECONDS);
        releaseSlow.countDown();
    }

    private static Flow.Subscriber<Integer> subscriber(
        java.util.function.IntConsumer onNext,
        CompletableFuture<Void> completed) {
        return new Flow.Subscriber<>() {
            @Override public void onSubscribe(Flow.Subscription subscription) {
                subscription.request(Long.MAX_VALUE);
            }

            @Override public void onNext(Integer item) {
                onNext.accept(item);
            }

            @Override public void onError(Throwable failure) {
                completed.completeExceptionally(failure);
            }

            @Override public void onComplete() {
                completed.complete(null);
            }
        };
    }
}
