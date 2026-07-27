package systems.zlink.framework.runtime.internal.monitoring;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import systems.zlink.framework.monitoring.ZLinkRuntimeEvent;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventHandler;

public final class ZLinkRuntimeEventDispatcher {
    private final List<Registration<?>> registrations = new ArrayList<>();
    private final AtomicInteger handlerFailureCount = new AtomicInteger();

    public synchronized <TEvent extends ZLinkRuntimeEvent> AutoCloseable register(
        Class<TEvent> eventType,
        ZLinkRuntimeEventHandler<TEvent> handler) {
        Registration<TEvent> registration = new Registration<>(eventType, handler);
        registrations.add(registration);
        return () -> remove(registration);
    }

    public void publish(ZLinkRuntimeEvent event) {
        List<Registration<?>> snapshot;
        synchronized (this) {
            snapshot = List.copyOf(registrations);
        }
        for (Registration<?> registration : snapshot) {
            dispatch(registration, event);
        }
    }

    public int handlerFailureCount() {
        return handlerFailureCount.get();
    }

    private synchronized void remove(Registration<?> registration) {
        registrations.remove(registration);
    }

    private <TEvent extends ZLinkRuntimeEvent> void dispatch(
        Registration<TEvent> registration,
        ZLinkRuntimeEvent event) {
        if (!registration.eventType().isInstance(event)) {
            return;
        }
        try {
            registration.handler().handle(registration.eventType().cast(event));
        } catch (RuntimeException ex) {
            handlerFailureCount.incrementAndGet();
        }
    }

    private record Registration<TEvent extends ZLinkRuntimeEvent>(
        Class<TEvent> eventType,
        ZLinkRuntimeEventHandler<TEvent> handler) {
    }
}
