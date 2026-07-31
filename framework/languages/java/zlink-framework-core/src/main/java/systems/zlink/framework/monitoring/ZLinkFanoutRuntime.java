package systems.zlink.framework.monitoring;

import java.util.concurrent.Flow;

public interface ZLinkFanoutRuntime {
    ZLinkFanoutStatus snapshot(String channelName);

    Flow.Publisher<ZLinkFanoutStatus> observe(
        String channelName,
        int capacity);
}
