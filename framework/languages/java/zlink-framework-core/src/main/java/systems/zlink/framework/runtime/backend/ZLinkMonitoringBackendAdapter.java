package systems.zlink.framework.runtime.backend;

public interface ZLinkMonitoringBackendAdapter {
    ZLinkBackendSocketMonitor openSocketMonitor(ZLinkBackendSocket socket);
}
