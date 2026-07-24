namespace Zlink.Framework.Runtime.Actors;

internal sealed record ZLinkSessionBindingEntry(
    ZLinkSessionContext Context,
    string BindingToken,
    ZLinkSessionActor ActorRef);

internal readonly record struct ZLinkSessionBindingKey(
    string ActorId,
    string BindingToken);

internal sealed class ZLinkSessionActorBindingTable
{
    private readonly Dictionary<ZLinkSessionBindingKey, ZLinkSessionBindingEntry> _entries = new();

    public ZLinkSessionBindingEntry[] Bind(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken,
        ZLinkSessionActor actorRef)
    {
        lock (_entries)
        {
            var replaced = _entries
                .Where(entry => string.Equals(entry.Key.ActorId, actorId, StringComparison.Ordinal))
                .Select(entry => entry.Value)
                .ToArray();
            foreach (var entry in replaced)
                _entries.Remove(new ZLinkSessionBindingKey(actorId, entry.BindingToken));

            _entries[new ZLinkSessionBindingKey(actorId, bindingToken)] = new ZLinkSessionBindingEntry(
                context,
                bindingToken,
                actorRef);
            return replaced;
        }
    }

    public void Unbind(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken)
    {
        lock (_entries)
        {
            var key = new ZLinkSessionBindingKey(actorId, bindingToken);
            if (_entries.TryGetValue(key, out var existing)
                && ReferenceEquals(existing.Context, context)
                && string.Equals(existing.BindingToken, bindingToken, StringComparison.Ordinal))
                _entries.Remove(key);
        }
    }

    public bool TryGet(
        string actorId,
        string bindingToken,
        out ZLinkSessionContext context)
    {
        lock (_entries)
        {
            var key = new ZLinkSessionBindingKey(actorId, bindingToken);
            if (_entries.TryGetValue(key, out var entry))
            {
                context = entry.Context;
                return true;
            }

            context = null!;
            return false;
        }
    }

    public bool TryGetByActorId(
        string actorId,
        out ZLinkSessionContext context)
    {
        lock (_entries)
        {
            foreach (var entry in _entries)
                if (string.Equals(entry.Key.ActorId, actorId, StringComparison.Ordinal))
                {
                    context = entry.Value.Context;
                    return true;
                }

            context = null!;
            return false;
        }
    }

    public void ResetGeneration()
    {
        lock (_entries) _entries.Clear();
    }
}
