package systems.zlink.framework.configuration;

import java.time.Duration;
import java.util.Optional;
import org.jspecify.annotations.Nullable;

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

    /** Sets the send timeout, or clears it to the one-second default when null. */
    void setSendTimeout(@Nullable Duration value);
}
