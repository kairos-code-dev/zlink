package systems.zlink.framework.configuration;

import java.time.Duration;
import java.util.Optional;

public interface ZLinkMeshNodeSocketConfig {
    long maxMessageSize();

    void setMaxMessageSize(long value);

    int sendHighWaterMark();

    void setSendHighWaterMark(int value);

    int receiveHighWaterMark();

    void setReceiveHighWaterMark(int value);

    Optional<Duration> receiveTimeout();

    void setReceiveTimeout(Duration value);

    Optional<Duration> sendTimeout();

    void setSendTimeout(Duration value);
}
