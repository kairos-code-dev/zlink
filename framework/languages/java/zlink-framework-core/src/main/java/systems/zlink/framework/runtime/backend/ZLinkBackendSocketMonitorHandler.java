package systems.zlink.framework.runtime.backend;

@FunctionalInterface
public interface ZLinkBackendSocketMonitorHandler {
    void handle(ZLinkBackendSocketMonitorEvent event);
}
