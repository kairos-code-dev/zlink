using StackExchange.Redis;
using Systems.Zlink;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.Server.Configuration;

internal interface IRoomRouteStore
{
    ValueTask SaveAsync(
        string roomId,
        RoomRoute route,
        CancellationToken cancellationToken);

    ValueTask<RoomRoute> LoadAsync(
        string roomId,
        CancellationToken cancellationToken);
}

internal sealed record RoomRoute(
    string RouteChannelId,
    string OwnerNodeRid,
    string SpotRid,
    string SpotKind);

internal sealed class RedisRoomRouteStore : IRoomRouteStore, IAsyncDisposable
{
    private readonly IDatabase _database;
    private readonly string _keyPrefix;
    private readonly ConnectionMultiplexer _redis;

    public RedisRoomRouteStore(SampleSettings settings)
    {
        _keyPrefix = settings.RedisKeyPrefix;
        _redis = ConnectionMultiplexer.Connect(settings.RedisEndpoint);
        _database = _redis.GetDatabase();
    }

    public async ValueTask DisposeAsync()
    {
        await _redis.CloseAsync().ConfigureAwait(false);
        _redis.Dispose();
    }

    public async ValueTask SaveAsync(
        string roomId,
        RoomRoute route,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        await _database.HashSetAsync(
            Key(roomId),
            new HashEntry[]
            {
                new("RouteChannelId", route.RouteChannelId),
                new("OwnerNodeRid", route.OwnerNodeRid),
                new("SpotRid", route.SpotRid),
                new("SpotKind", route.SpotKind)
            }).ConfigureAwait(false);
    }

    public async ValueTask<RoomRoute> LoadAsync(
        string roomId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var values = await _database.HashGetAllAsync(Key(roomId)).ConfigureAwait(false);
        if (values.Length == 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotRouteNotFound,
                $"SPOT route was not found for room '{roomId}'.",
                true);

        var map = values.ToDictionary(
            static entry => (string)entry.Name!,
            static entry => (string)entry.Value!,
            StringComparer.Ordinal);
        return new RoomRoute(
            Require(map, "RouteChannelId", roomId),
            Require(map, "OwnerNodeRid", roomId),
            Require(map, "SpotRid", roomId),
            Require(map, "SpotKind", roomId));
    }

    private string Key(string roomId)
    {
        return $"{_keyPrefix}rooms:{roomId}";
    }

    private static string Require(
        IReadOnlyDictionary<string, string> values,
        string name,
        string roomId)
    {
        if (values.TryGetValue(name, out var value) && !string.IsNullOrWhiteSpace(value)) return value;

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.SpotRouteNotFound,
            $"SPOT route field '{name}' was not found for room '{roomId}'.",
            true);
    }
}

internal sealed class RedisSpotRouteRefResolver(IRoomRouteStore routes)
    : IZLinkSpotRouteRefResolver
{
    public async ValueTask<ZLinkSpotRouteRef> ResolveSpotRouteRefAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        var roomId = spotRid.ToString();
        var route = await routes.LoadAsync(roomId, cancellationToken).ConfigureAwait(false);
        return new ZLinkSpotRouteRef(
            route.RouteChannelId,
            RoutingId.From(route.OwnerNodeRid),
            RoutingId.From(route.SpotRid),
            Enum.Parse<ZLinkSpotKind>(route.SpotKind, false));
    }
}
