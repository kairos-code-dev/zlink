package systems.zlink.framework.spring;

import systems.zlink.framework.monitoring.ZLinkMonitoringOptions;

public interface ZLinkMonitoringOptionsCustomizer {
    void customize(ZLinkMonitoringOptions options);
}
