namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorSessionRegistry
{
    private readonly object _gate = new();
    private readonly Dictionary<string, ZLinkActorRuntimeState> _states = new(StringComparer.Ordinal);

    public ZLinkActorRuntimeState GetOrCreate(string actorId)
    {
        lock (_gate)
        {
            if (_states.TryGetValue(actorId, out var existing))
            {
                return existing;
            }

            var created = new ZLinkActorRuntimeState(actorId);
            _states.Add(actorId, created);
            return created;
        }
    }

    public bool TryGet(string actorId, out ZLinkActorRuntimeState state)
    {
        lock (_gate)
        {
            return _states.TryGetValue(actorId, out state!);
        }
    }

    public void TryRemove(string actorId, ZLinkActorRuntimeState state)
    {
        lock (_gate)
        {
            if (_states.TryGetValue(actorId, out var existing)
                && ReferenceEquals(existing, state)
                && state.SessionId is null
                && state.Activation is null)
            {
                _states.Remove(actorId);
            }
        }
    }
}
