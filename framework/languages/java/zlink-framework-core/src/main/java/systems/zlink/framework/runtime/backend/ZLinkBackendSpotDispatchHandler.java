package systems.zlink.framework.runtime.backend;

@FunctionalInterface
public interface ZLinkBackendSpotDispatchHandler {
    void handle(ZLinkBackendSpotDispatchInfo info);
}
