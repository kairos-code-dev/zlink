package systems.zlink.framework.runtime.backend;

public interface ZLinkBackendSocketMonitor extends ZLinkBackendObject {
    void onEvent(ZLinkBackendSocketMonitorHandler handler);

    ZLinkBackendSocketMonitorEvent recv();
}
