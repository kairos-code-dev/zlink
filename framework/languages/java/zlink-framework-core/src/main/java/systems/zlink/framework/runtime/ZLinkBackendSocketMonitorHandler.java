package systems.zlink.framework.runtime;

@FunctionalInterface
public interface ZLinkBackendSocketMonitorHandler {
    void handle(ZLinkBackendSocketMonitorEvent event);
}
