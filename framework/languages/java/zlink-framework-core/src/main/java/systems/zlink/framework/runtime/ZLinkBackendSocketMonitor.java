package systems.zlink.framework.runtime;

public interface ZLinkBackendSocketMonitor extends ZLinkBackendObject {
    void onEvent(ZLinkBackendSocketMonitorHandler handler);

    ZLinkBackendSocketMonitorEvent recv();
}
