package systems.zlink.framework.runtime.backend;

@FunctionalInterface
public interface ZLinkBackendRequestCallback {
    void handle(ZLinkBackendReceived reply);
}
