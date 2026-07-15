package systems.zlink.framework.errors;

/**
 * Raised when a {@code runWorker(...)} task throws. The original failure is
 * preserved as the cause.
 */
public class ZLinkWorkerFailedException extends ZLinkFrameworkException {
    public ZLinkWorkerFailedException(String message, Throwable cause) {
        super(ZLinkFrameworkErrorKind.WORKER_FAILED, message, cause);
    }
}
