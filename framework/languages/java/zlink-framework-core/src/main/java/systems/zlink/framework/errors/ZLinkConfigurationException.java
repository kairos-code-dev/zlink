package systems.zlink.framework.errors;

public final class ZLinkConfigurationException extends ZLinkFrameworkException {
    public ZLinkConfigurationException(String message) {
        super(message);
    }

    public ZLinkConfigurationException(String message, Throwable cause) {
        super(message, cause);
    }
}
