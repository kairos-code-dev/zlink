package systems.zlink.samples.deliverydispatch.server.configuration;

import java.time.Duration;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;

public final class SampleLocationStore {
    private SampleLocationStore() {
    }

    public static ZLinkRedisLocationStore create() {
        return new ZLinkRedisLocationStore(
            new ZLinkRedisLocationOptions()
                .setConnectionString(SampleTopology.RedisEndpoint)
                .setKeyPrefix(SampleTopology.RedisKeyPrefix + "locations:")
                .setCommandTimeout(Duration.ofMillis(500)));
    }
}
