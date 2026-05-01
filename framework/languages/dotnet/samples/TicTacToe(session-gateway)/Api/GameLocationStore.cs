using TicTacToe.SessionGateway.Contracts;
using Zlink;

namespace TicTacToe.SessionGateway.Api;

internal sealed class GameLocationStore(RoutingId playNodeRid)
{
    private readonly object _gate = new();
    private readonly Dictionary<string, ResolveGameRes> _games = new(StringComparer.Ordinal);

    public CreateGameRes Create(string preferredGameId)
    {
        lock (_gate)
        {
            var gameId = string.IsNullOrWhiteSpace(preferredGameId)
                ? Guid.NewGuid().ToString("N")
                : preferredGameId;
            _games.TryAdd(gameId, new ResolveGameRes(gameId, playNodeRid.ToHex()));
            return new CreateGameRes(gameId);
        }
    }

    public ResolveGameRes Resolve(string gameId)
    {
        lock (_gate)
        {
            return _games.TryGetValue(gameId, out var location)
                ? location
                : throw new InvalidOperationException($"Game '{gameId}' is not registered.");
        }
    }
}
