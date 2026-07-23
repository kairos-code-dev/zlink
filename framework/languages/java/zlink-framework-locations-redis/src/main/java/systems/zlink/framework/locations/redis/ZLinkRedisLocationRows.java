package systems.zlink.framework.locations.redis;

import io.lettuce.core.KeyValue;
import io.lettuce.core.ScanArgs;
import io.lettuce.core.ScanCursor;
import io.lettuce.core.ValueScanCursor;
import io.lettuce.core.api.async.RedisAsyncCommands;
import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Collection;
import java.util.Comparator;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Predicate;
import systems.zlink.framework.locations.ZLinkLocationPage;
import systems.zlink.framework.locations.ZLinkClientServerServerDescriptor;
import systems.zlink.framework.locations.ZLinkPageRequest;

final class ZLinkRedisLocationRows {
    private static final String[] ROW_FIELDS = {"json", "gen", "updatedAtMs"};
    private static final int DESCRIPTOR_PAGE_MAX_BYTES =
        4 * 1024 * 1024;

    private final ZLinkRedisLocationConnection connection;
    private final ZLinkRedisLocationKeys keys;

    ZLinkRedisLocationRows(
        ZLinkRedisLocationConnection connection,
        ZLinkRedisLocationKeys keys) {
        this.connection = connection;
        this.keys = keys;
    }

    <T> CompletionStage<T> resolve(String tag, String rowKey, RowDeserializer<T> deserializer) {
        return connection.commands()
            .thenCompose(redis -> redis.hmget(keys.rowHashKey(tag, rowKey), ROW_FIELDS))
            .thenApply(fields -> materialize(fields, deserializer));
    }

    <T> CompletionStage<List<T>> listRows(String tag, RowDeserializer<T> deserializer) {
        return connection.commands()
            .thenCompose(redis -> redis.smembers(keys.kindIndexKey(tag))
                .thenCompose(rowKeys -> loadRows(redis, tag, rowKeys, deserializer)));
    }

    <T> CompletionStage<ZLinkLocationPage<T>> listPage(
        String tag,
        RowDeserializer<T> deserializer,
        Predicate<T> matches,
        ZLinkPageRequest page) {
        ZLinkPageRequest safePage = page == null ? ZLinkPageRequest.firstPage() : page;
        if (safePage.pageSize() <= 0) {
            return listRows(tag, deserializer)
                .thenApply(rows -> new ZLinkLocationPage<>(
                    rows.stream().filter(matches).toList(),
                    null));
        }

        String cursor = safePage.continuationToken() == null || safePage.continuationToken().isBlank()
            ? "0"
            : safePage.continuationToken();
        ScanArgs scanArgs = ScanArgs.Builder.limit(safePage.pageSize());
        return connection.commands()
            .thenCompose(redis -> redis.sscan(keys.kindIndexKey(tag), ScanCursor.of(cursor), scanArgs)
                .thenCompose(scan -> loadRows(redis, tag, scan.getValues(), deserializer)
                    .thenApply(rows -> toScannedPage(scan, rows, matches))));
    }

    CompletionStage<ZLinkLocationPage<ZLinkClientServerServerDescriptor>>
        listClientServers(
            String channelName,
            ZLinkPageRequest page) {
        ZLinkPageRequest safePage =
            page == null ? ZLinkPageRequest.firstPage() : page;
        int pageSize =
            safePage.pageSize() <= 0 ? 100 : safePage.pageSize();
        if (pageSize > 1000) {
            throw new IllegalArgumentException(
                "ClientServer page size must be in 1..1000");
        }
        int offset = decodeClientServerCursor(
            channelName,
            safePage.continuationToken());
        return connection.commands()
            .thenCompose(redis -> redis.zrange(
                    keys.clientServerDescriptorChannelIndexKey(
                        channelName),
                    offset,
                    offset + pageSize)
                .thenCompose(members -> loadClientServerPage(
                        redis,
                        members,
                        pageSize)
                    .thenApply(loaded -> {
                        boolean hasMore =
                            loaded.consumed() < members.size()
                                || members.size() > pageSize;
                        return new ZLinkLocationPage<>(
                            List.copyOf(loaded.items()),
                            hasMore
                                ? encodeClientServerCursor(
                                    channelName,
                                    offset + loaded.consumed())
                                : null);
                    })));
    }

    private CompletionStage<ClientServerPageAccumulator>
        loadClientServerPage(
            RedisAsyncCommands<String, String> redis,
            List<String> members,
            int pageSize) {
        return loadClientServerPage(
            redis,
            members,
            pageSize,
            0,
            new ClientServerPageAccumulator(
                new ArrayList<>(),
                0,
                0));
    }

    private CompletionStage<ClientServerPageAccumulator>
        loadClientServerPage(
            RedisAsyncCommands<String, String> redis,
            List<String> members,
            int pageSize,
            int index,
            ClientServerPageAccumulator accumulator) {
        if (index >= members.size()
            || accumulator.items().size() >= pageSize) {
            return CompletableFuture.completedFuture(accumulator);
        }
        String rowKey = members.get(index);
        return redis.hmget(
                keys.clientServerDescriptorRowKey(rowKey),
                ROW_FIELDS)
            .thenCompose(fields -> {
                String json = field(fields, "json");
                if (json == null) {
                    return loadClientServerPage(
                        redis,
                        members,
                        pageSize,
                        index + 1,
                        new ClientServerPageAccumulator(
                            accumulator.items(),
                            accumulator.encodedBytes(),
                            accumulator.consumed() + 1));
                }
                int rowBytes =
                    json.getBytes(StandardCharsets.UTF_8).length;
                if (!accumulator.items().isEmpty()
                    && accumulator.encodedBytes() + rowBytes
                        > DESCRIPTOR_PAGE_MAX_BYTES) {
                    return CompletableFuture.completedFuture(
                        accumulator);
                }
                ZLinkClientServerServerDescriptor row =
                    materialize(
                        fields,
                        ZLinkRedisLocationRowJson
                            ::deserializeClientServer);
                if (row != null) {
                    accumulator.items().add(row);
                }
                return loadClientServerPage(
                    redis,
                    members,
                    pageSize,
                    index + 1,
                    new ClientServerPageAccumulator(
                        accumulator.items(),
                        accumulator.encodedBytes() + rowBytes,
                        accumulator.consumed() + 1));
            });
    }

    private static String encodeClientServerCursor(
        String channelName,
        int offset) {
        return Base64.getUrlEncoder()
            .withoutPadding()
            .encodeToString(
                (channelName + "\0" + offset)
                    .getBytes(StandardCharsets.UTF_8));
    }

    private static int decodeClientServerCursor(
        String channelName,
        String token) {
        if (token == null || token.isBlank()) {
            return 0;
        }
        if (token.getBytes(StandardCharsets.UTF_8).length > 4096) {
            throw new IllegalArgumentException(
                "ClientServer continuation token is invalid");
        }
        try {
            String decoded = new String(
                Base64.getUrlDecoder().decode(token),
                StandardCharsets.UTF_8);
            int separator = decoded.lastIndexOf('\0');
            if (separator <= 0
                || !decoded.substring(0, separator)
                    .equals(channelName)) {
                throw new IllegalArgumentException();
            }
            int offset = Integer.parseInt(
                decoded.substring(separator + 1));
            if (offset < 0) {
                throw new IllegalArgumentException();
            }
            return offset;
        } catch (IllegalArgumentException error) {
            throw new IllegalArgumentException(
                "ClientServer continuation token is invalid",
                error);
        }
    }

    private record ClientServerPageAccumulator(
        List<ZLinkClientServerServerDescriptor> items,
        int encodedBytes,
        int consumed) {
    }

    private <T> CompletionStage<List<T>> loadRows(
        RedisAsyncCommands<String, String> redis,
        String tag,
        Collection<String> rowKeys,
        RowDeserializer<T> deserializer) {
        List<String> ordered = rowKeys.stream().sorted(Comparator.naturalOrder()).toList();
        List<CompletableFuture<T>> reads = ordered.stream()
            .map(rowKey -> redis.hmget(keys.rowHashKey(tag, rowKey), ROW_FIELDS)
                .thenApply(fields -> materialize(fields, deserializer))
                .toCompletableFuture())
            .toList();
        return CompletableFuture.allOf(reads.toArray(CompletableFuture[]::new))
            .thenApply(ignored -> {
                List<T> rows = new ArrayList<>(reads.size());
                for (CompletableFuture<T> read : reads) {
                    T row = read.getNow(null);
                    if (row != null) {
                        rows.add(row);
                    }
                }
                return List.copyOf(rows);
            });
    }

    private static <T> ZLinkLocationPage<T> toScannedPage(
        ValueScanCursor<String> scan,
        List<T> rows,
        Predicate<T> matches) {
        String continuation = scan.isFinished() ? null : scan.getCursor();
        return new ZLinkLocationPage<>(
            rows.stream().filter(matches).toList(),
            continuation);
    }

    private <T> T materialize(List<KeyValue<String, String>> fields, RowDeserializer<T> deserializer) {
        String json = field(fields, "json");
        if (json == null) {
            return null;
        }
        long generation = Long.parseLong(field(fields, "gen"));
        Instant updatedAt = Instant.ofEpochMilli(Long.parseLong(field(fields, "updatedAtMs")));
        return deserializer.deserialize(json, generation, updatedAt);
    }

    private static String field(List<KeyValue<String, String>> fields, String name) {
        return fields.stream()
            .filter(field -> Objects.equals(field.getKey(), name))
            .findFirst()
            .filter(KeyValue::hasValue)
            .map(KeyValue::getValue)
            .orElse(null);
    }

    @FunctionalInterface
    interface RowDeserializer<T> {
        T deserialize(String json, long generation, Instant updatedAt);
    }
}
