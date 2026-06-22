package systems.zlink.samples.tictactoe.server.configuration;

import io.lettuce.core.RedisClient;
import io.lettuce.core.api.StatefulRedisConnection;
import java.util.Map;
import systems.zlink.samples.tictactoe.server.play.application.gamecreation.TicTacToeGameCreator.GameRoomRoute;
import systems.zlink.samples.tictactoe.server.play.application.gamecreation.TicTacToeGameCreator.GameRoomRouteWriter;

public final class RedisRoomRouteStore implements GameRoomRouteWriter, AutoCloseable {
    private final RedisClient client;
    private final StatefulRedisConnection<String, String> connection;
    private final String keyPrefix;

    public RedisRoomRouteStore(SampleSettings settings) {
        this.client = RedisClient.create(redisUri(settings.redisEndpoint()));
        this.connection = client.connect();
        this.keyPrefix = settings.redisKeyPrefix();
    }

    public void save(RoomRoute route) {
        connection.sync().hset(key(route.roomId()), Map.of(
            "ownerPlayEndpoint", route.ownerPlayEndpoint(),
            "ownerSpotEndpoint", route.ownerSpotEndpoint(),
            "ownerSpotNodeRid", route.ownerSpotNodeRid()));
    }

    @Override
    public void save(GameRoomRoute route) {
        save(new RoomRoute(
            route.roomId(),
            route.ownerPlayEndpoint(),
            route.ownerSpotEndpoint(),
            route.ownerSpotNodeRid()));
    }

    public RoomRoute require(String roomId) {
        Map<String, String> values = connection.sync().hgetall(key(roomId));
        if (values == null || values.isEmpty()) {
            throw new IllegalStateException("room route was not found: " + roomId);
        }
        return new RoomRoute(
            roomId,
            values.get("ownerPlayEndpoint"),
            values.get("ownerSpotEndpoint"),
            values.get("ownerSpotNodeRid"));
    }

    @Override
    public void close() {
        connection.close();
        client.shutdown();
    }

    private String key(String roomId) {
        return keyPrefix + roomId;
    }

    private static String redisUri(String endpoint) {
        return endpoint.startsWith("redis://") ? endpoint : "redis://" + endpoint;
    }

    public record RoomRoute(
        String roomId,
        String ownerPlayEndpoint,
        String ownerSpotEndpoint,
        String ownerSpotNodeRid) {
    }
}
