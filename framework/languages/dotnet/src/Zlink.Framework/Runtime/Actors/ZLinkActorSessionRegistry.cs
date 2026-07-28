namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorSessionRegistry(
    Action<string>? handoffDiagnostic = null,
    TimeSpan? sessionBindingTombstoneRetention = null)
{
    private readonly object _gate = new();
    private readonly Dictionary<string, ZLinkActorRuntimeState> _states = new(StringComparer.Ordinal);

    public ZLinkActorRuntimeState GetOrCreate(string actorId)
    {
        lock (_gate)
        {
            if (_states.TryGetValue(actorId, out var existing)) return existing;

            var created = new ZLinkActorRuntimeState(
                actorId,
                handoffDiagnostic: handoffDiagnostic,
                sessionBindingTombstoneRetention:
                    sessionBindingTombstoneRetention);
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
                _states.Remove(actorId);
        }
    }

    public void RemoveIfCurrent(string actorId, ZLinkActorRuntimeState state)
    {
        lock (_gate)
        {
            if (_states.TryGetValue(actorId, out var existing)
                && ReferenceEquals(existing, state))
                _states.Remove(actorId);
        }
    }

    public void ResetGeneration()
    {
        ZLinkActorRuntimeState[] states;
        lock (_gate)
        {
            states = _states.Values.ToArray();
            _states.Clear();
        }

        foreach (var state in states) state.InvalidateRuntimeGeneration();
    }

    public ZLinkActorRuntimeState[] Snapshot()
    {
        lock (_gate) return _states.Values.ToArray();
    }
}
