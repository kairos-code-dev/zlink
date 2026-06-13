package systems.zlink.framework.spots;

import systems.zlink.framework.CancellationToken;

/**
 * Work executed on the framework worker pool, outside the owning Spot's serial
 * execution line.
 *
 * <p>The task must not touch Spot or Entry Spot state and must not use the Spot
 * context outbound surface; Spot state changes belong in the completion callback or
 * continuation, which runs back on the owning Spot's serial line. The cancellation
 * token is signalled when the call times out; cooperative tasks should observe it
 * and stop early.
 */
@FunctionalInterface
public interface ZLinkWorkerTask<T> {
    T run(CancellationToken cancellationToken) throws Exception;
}
