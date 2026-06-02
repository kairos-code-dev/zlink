package systems.zlink.framework.runtime;

@FunctionalInterface
public interface ZLinkBackendRequestCallback {
    void handle(ZLinkBackendReceived reply);
}
