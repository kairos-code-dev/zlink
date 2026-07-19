package systems.zlink.framework.configuration;

import java.time.Duration;
import java.util.Optional;

public interface ZLinkSpotPublisherConfig {
    int sendHighWaterMark();

    void setSendHighWaterMark(int value);

    Optional<Duration> sendTimeout();

    void setSendTimeout(Duration value);

    Optional<Duration> linger();

    void setLinger(Duration value);
}
