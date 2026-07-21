package systems.zlink.framework.runtime.messaging;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicBoolean;

/** Keeps the single-submit state inside one one-way call object. */
public final class ZLinkOneWayCallGate {
    private final AtomicBoolean submitted = new AtomicBoolean();

    /** Returns an exceptional stage when this call was already submitted. */
    public <T> CompletionStage<T> begin() {
        if (submitted.compareAndSet(false, true)) {
            return null;
        }
        return CompletableFuture.failedFuture(
            new IllegalStateException("call has already been submitted"));
    }
}
