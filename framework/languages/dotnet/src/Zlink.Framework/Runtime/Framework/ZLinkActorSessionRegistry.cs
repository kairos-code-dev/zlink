namespace Zlink.Framework.Runtime.Framework;

internal sealed class ZLinkActorSessionRegistry
{
    private readonly object _gate = new();
    private readonly Dictionary<string, ZLinkActorRuntimeState> _states = new(StringComparer.Ordinal);

    public ZLinkActorRuntimeState GetOrCreate(string actorKey)
    {
        lock (_gate)
        {
            if (_states.TryGetValue(actorKey, out var existing))
            {
                return existing;
            }

            var created = new ZLinkActorRuntimeState();
            _states.Add(actorKey, created);
            return created;
        }
    }

    public void TryRemove(string actorKey, ZLinkActorRuntimeState state)
    {
        lock (_gate)
        {
            if (_states.TryGetValue(actorKey, out var existing)
                && ReferenceEquals(existing, state)
                && state.SessionId is null
                && state.Activation is null)
            {
                _states.Remove(actorKey);
            }
        }
    }
}
