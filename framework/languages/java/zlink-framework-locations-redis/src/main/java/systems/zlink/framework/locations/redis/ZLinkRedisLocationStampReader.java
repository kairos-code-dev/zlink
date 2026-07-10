package systems.zlink.framework.locations.redis;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.locations.ZLinkLocationChangeStampScope;

final class ZLinkRedisLocationStampReader {
    private final ZLinkRedisLocationConnection connection;
    private final ZLinkRedisLocationKeys keys;

    ZLinkRedisLocationStampReader(
        ZLinkRedisLocationConnection connection,
        ZLinkRedisLocationKeys keys) {
        this.connection = connection;
        this.keys = keys;
    }

    CompletionStage<Long> getChangeStampAsync(ZLinkLocationChangeStampScope scope) {
        return connection.commands()
            .thenCompose(redis -> redis.get(keys.stampKey(
                ZLinkRedisLocationKeys.tagOf(scope.kind()),
                scope.meshName())))
            .thenApply(value -> value == null ? 0L : Long.parseLong(value));
    }
}
