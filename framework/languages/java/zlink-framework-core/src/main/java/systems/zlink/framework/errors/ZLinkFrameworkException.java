package systems.zlink.framework.errors;

public class ZLinkFrameworkException extends RuntimeException {
    public ZLinkFrameworkException(String message) {
        super(message);
    }

    public ZLinkFrameworkException(String message, Throwable cause) {
        super(message, cause);
    }
}
