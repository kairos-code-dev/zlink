package systems.zlink.framework.runtime;

public interface ZLinkMonitoringBackendAdapter {
    ZLinkBackendSocketMonitor openSocketMonitor(ZLinkBackendSocket socket);
}
