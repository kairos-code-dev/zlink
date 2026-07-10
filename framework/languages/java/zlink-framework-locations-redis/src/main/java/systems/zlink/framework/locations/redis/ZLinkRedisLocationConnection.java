package systems.zlink.framework.locations.redis;

import io.lettuce.core.RedisClient;
import io.lettuce.core.api.StatefulRedisConnection;
import io.lettuce.core.api.async.RedisAsyncCommands;
import io.lettuce.core.codec.StringCodec;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicReference;

final class ZLinkRedisLocationConnection {
    private final ZLinkRedisLocationOptions options;
    private final RedisClient client;
    private final AtomicReference<CompletableFuture<StatefulRedisConnection<String, String>>> connection =
        new AtomicReference<>();

    ZLinkRedisLocationConnection(ZLinkRedisLocationOptions options) {
        this.options = options;
        this.client = RedisClient.create(options.redisUri());
    }

    CompletionStage<RedisAsyncCommands<String, String>> commands() {
        return connection().thenApply(StatefulRedisConnection::async);
    }

    CompletionStage<Void> closeAsync() {
        CompletableFuture<StatefulRedisConnection<String, String>> current = connection.getAndSet(null);
        CompletionStage<Void> closed = current == null
            ? CompletableFuture.completedFuture(null)
            : current.thenCompose(StatefulRedisConnection::closeAsync);
        return closed.thenCompose(ignored -> client.shutdownAsync()).thenApply(ignored -> null);
    }

    private CompletionStage<StatefulRedisConnection<String, String>> connection() {
        CompletableFuture<StatefulRedisConnection<String, String>> existing = connection.get();
        if (existing != null) {
            return existing;
        }

        CompletableFuture<StatefulRedisConnection<String, String>> created =
            client.connectAsync(StringCodec.UTF8, options.redisUri()).toCompletableFuture();
        created.whenComplete((ignored, failure) -> {
            if (failure != null) {
                connection.compareAndSet(created, null);
            }
        });
        if (connection.compareAndSet(null, created)) {
            return created;
        }
        return connection.get();
    }
}
