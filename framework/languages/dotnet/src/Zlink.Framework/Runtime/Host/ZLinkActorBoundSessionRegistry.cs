namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkActorBoundSessionRegistry
{
    private readonly Dictionary<string, List<Entry>> _entries = new(StringComparer.Ordinal);
    private readonly object _gate = new();

    public void Register(
        ZLinkFrameworkRuntime runtime,
        string actorId,
        RoutingId sessionRid,
        string bindingToken)
    {
        if (!ZLinkActorBoundSessionBindingToken.IsNative(bindingToken)) return;

        var key = sessionRid.ToHex();
        lock (_gate)
        {
            if (!_entries.TryGetValue(key, out var entries))
            {
                entries = [];
                _entries[key] = entries;
            }

            entries.RemoveAll(entry => entry.Matches(runtime, actorId, bindingToken) || !entry.IsAlive);
            entries.Add(new Entry(new WeakReference<ZLinkFrameworkRuntime>(runtime), actorId, bindingToken));
        }
    }

    public void Unregister(
        ZLinkFrameworkRuntime runtime,
        string actorId,
        string bindingToken)
    {
        if (!ZLinkActorBoundSessionBindingToken.IsNative(bindingToken)) return;

        lock (_gate)
        {
            foreach (var key in _entries.Keys.ToArray())
            {
                var entries = _entries[key];
                entries.RemoveAll(entry => entry.Matches(runtime, actorId, bindingToken) || !entry.IsAlive);
                if (entries.Count == 0) _entries.Remove(key);
            }
        }
    }

    public void Cleanup(RoutingId sessionRid)
    {
        Entry[] entries;
        var key = sessionRid.ToHex();
        lock (_gate)
        {
            if (!_entries.Remove(key, out var registered)) return;

            entries = registered.ToArray();
        }

        foreach (var entry in entries)
            if (entry.Runtime.TryGetTarget(out var runtime))
                runtime.UnbindActorSession(entry.ActorId, entry.BindingToken);
    }

    public void UnregisterRuntime(ZLinkFrameworkRuntime runtime)
    {
        lock (_gate)
        {
            foreach (var key in _entries.Keys.ToArray())
            {
                var entries = _entries[key];
                entries.RemoveAll(entry => entry.BelongsTo(runtime) || !entry.IsAlive);
                if (entries.Count == 0) _entries.Remove(key);
            }
        }
    }

    private sealed record Entry(
        WeakReference<ZLinkFrameworkRuntime> Runtime,
        string ActorId,
        string BindingToken)
    {
        public bool IsAlive => Runtime.TryGetTarget(out _);

        public bool Matches(
            ZLinkFrameworkRuntime runtime,
            string actorId,
            string bindingToken)
        {
            return Runtime.TryGetTarget(out var current)
                   && ReferenceEquals(current, runtime)
                   && string.Equals(ActorId, actorId, StringComparison.Ordinal)
                   && string.Equals(BindingToken, bindingToken, StringComparison.Ordinal);
        }

        public bool BelongsTo(ZLinkFrameworkRuntime runtime)
            => Runtime.TryGetTarget(out var current) && ReferenceEquals(current, runtime);
    }
}
