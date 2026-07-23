package systems.zlink.framework.locations.redis;

import io.lettuce.core.RedisClient;
import io.lettuce.core.RedisURI;
import io.lettuce.core.api.StatefulRedisConnection;
import io.lettuce.core.api.async.RedisAsyncCommands;
import io.lettuce.core.codec.StringCodec;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

final class ZLinkRedisLocationConnection {
    private final RedisURI redisUri;
    private final RedisClient client;
    private CompletableFuture<StatefulRedisConnection<String, String>> connection;
    private CompletionStage<Void> closeStage;
    private boolean closed;

    ZLinkRedisLocationConnection(ZLinkRedisLocationOptions options) {
        this(options.redisUri());
    }

    ZLinkRedisLocationConnection(RedisURI redisUri) {
        this.redisUri = redisUri;
        this.client = RedisClient.create(redisUri);
    }

    CompletionStage<RedisAsyncCommands<String, String>> commands() {
        return connection().thenApply(StatefulRedisConnection::async);
    }

    synchronized CompletionStage<Void> closeAsync() {
        if (closeStage != null) {
            return closeStage;
        }
        closed = true;
        CompletableFuture<StatefulRedisConnection<String, String>> current = connection;
        connection = null;
        CompletionStage<Void> connectionClosed = current == null
            ? CompletableFuture.completedFuture(null)
            : current.handle((connected, failure) -> connected)
                .thenCompose(connected -> connected == null
                    ? CompletableFuture.completedFuture(null)
                    : connected.closeAsync());
        closeStage = connectionClosed
            .thenCompose(ignored -> client.shutdownAsync())
            .thenApply(ignored -> null);
        return closeStage;
    }

    private synchronized CompletionStage<StatefulRedisConnection<String, String>> connection() {
        if (closed) {
            return CompletableFuture.failedFuture(
                new IllegalStateException("Redis location connection is closed."));
        }
        if (connection != null) {
            return connection;
        }

        CompletableFuture<StatefulRedisConnection<String, String>> created =
            client.connectAsync(StringCodec.UTF8, redisUri).toCompletableFuture();
        connection = created;
        created.whenComplete((ignored, failure) -> {
            if (failure != null) {
                clearFailedConnection(created);
            }
        });
        return created;
    }

    private synchronized void clearFailedConnection(
        CompletableFuture<StatefulRedisConnection<String, String>> failed) {
        if (connection == failed) {
            connection = null;
        }
    }
}
