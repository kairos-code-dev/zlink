package systems.zlink.samples.tictactoe.server.configuration;

import java.time.Duration;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;

public final class SampleLocationStore {
    private SampleLocationStore() {
    }

    public static ZLinkRedisLocationStore create(PlaySettings settings) {
        return new ZLinkRedisLocationStore(
            new ZLinkRedisLocationOptions()
                .setConnectionString(settings.redisEndpoint())
                .setKeyPrefix(settings.redisKeyPrefix() + "locations:")
                .setCommandTimeout(Duration.ofMillis(500)));
    }
}
