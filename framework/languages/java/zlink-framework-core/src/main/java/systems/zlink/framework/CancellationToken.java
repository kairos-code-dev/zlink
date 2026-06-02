package systems.zlink.framework;

/**
 * Cooperative cancellation signal passed through framework callbacks.
 */
public interface CancellationToken {
    boolean isCancellationRequested();
}
