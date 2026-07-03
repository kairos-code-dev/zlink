package systems.zlink.framework.monitoring;

import java.time.Duration;

public interface ZLinkMonitoringOptions {
    void addSocketEvents(String sourceName, ZLinkSocketEventKind... events);

    void addSpotEvents(String sourceName, Duration interval);

    void addLocationRuntimeEvents(String sourceName, Duration interval);
}
