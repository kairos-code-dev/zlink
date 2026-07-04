package systems.zlink.framework.errors;

public class ZLinkFrameworkException extends RuntimeException {
    private final ZLinkFrameworkErrorKind kind;
    private final boolean retriable;

    public ZLinkFrameworkException(String message) {
        this(ZLinkFrameworkErrorKind.REQUEST_FAILED, message);
    }

    public ZLinkFrameworkException(String message, Throwable cause) {
        this(ZLinkFrameworkErrorKind.REQUEST_FAILED, message, cause);
    }

    public ZLinkFrameworkException(ZLinkFrameworkErrorKind kind, String message) {
        this(kind, message, null, null);
    }

    public ZLinkFrameworkException(ZLinkFrameworkErrorKind kind, String message, Throwable cause) {
        this(kind, message, null, cause);
    }

    public ZLinkFrameworkException(
        ZLinkFrameworkErrorKind kind,
        String message,
        Boolean retriable,
        Throwable cause) {
        super(message, cause);
        this.kind = kind == null ? ZLinkFrameworkErrorKind.REQUEST_FAILED : kind;
        this.retriable = retriable == null ? this.kind.retriable() : retriable;
    }

    public ZLinkFrameworkErrorKind kind() {
        return kind;
    }

    public boolean retriable() {
        return retriable;
    }
}
