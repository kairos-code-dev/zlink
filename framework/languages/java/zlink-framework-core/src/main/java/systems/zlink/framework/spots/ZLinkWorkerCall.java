package systems.zlink.framework.spots;

import java.time.Duration;
import java.util.concurrent.CompletionStage;

/**
 * Fluent worker offload call created by {@code context.runWorker(...)}.
 *
 * <p>Two terminators are available.
 *
 * <ul>
 *   <li>{@link #submit()} returns an awaitable {@link CompletionStage}. When a
 *       handler returns this stage (gated awaitable path), the owning Spot does not
 *       start its next callback until the stage completes, and the continuation
 *       after completion re-enters the Spot serial line.</li>
 *   <li>{@link #submit(BiConsumer, BiConsumer)} registers detached callbacks
 *       (explicit interleaving opt-in). Both callbacks always execute on the owning
 *       Spot's serial line, never on the worker or submitting thread, so they must
 *       re-validate Spot state that may have changed since submission.</li>
 * </ul>
 *
 * <p>Failures are projected as {@code ZLinkWorkerQueueFullException},
 * {@code ZLinkWorkerTimeoutException}, or {@code ZLinkWorkerFailedException}. A late
 * result arriving after a timeout is dropped without invoking user callbacks again.
 */
public interface ZLinkWorkerCall<T> {
    ZLinkWorkerCall<T> timeout(Duration timeout);

    CompletionStage<T> submit();

}
