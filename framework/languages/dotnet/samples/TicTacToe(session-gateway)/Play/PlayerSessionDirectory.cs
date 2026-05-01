using Zlink;

namespace TicTacToe.SessionGateway.Play;

internal sealed class PlayerSessionDirectory
{
    private readonly object _gate = new();
    private readonly Dictionary<string, RoutingId> _sessions = new(StringComparer.Ordinal);

    public void Bind(
        string playerId,
        RoutingId sessionNodeRid)
    {
        lock (_gate)
        {
            _sessions[playerId] = sessionNodeRid;
        }
    }

    public RoutingId GetSessionNodeRid(string playerId)
    {
        lock (_gate)
        {
            return _sessions.TryGetValue(playerId, out var sessionNodeRid)
                ? sessionNodeRid
                : throw new InvalidOperationException($"No session is bound for player '{playerId}'.");
        }
    }
}
