namespace Zlink.Framework.Runtime.SessionActorDispatch;

internal sealed record ZLinkSessionBindingEntry(
    ZLinkSessionContext Context,
    string BindingToken,
    ZLinkActorRef ActorRef);

internal readonly record struct ZLinkSessionBindingKey(
    string ActorId,
    string BindingToken);

internal sealed class ZLinkSessionActorBindingTable
{
    private readonly Dictionary<ZLinkSessionBindingKey, ZLinkSessionBindingEntry> _entries = new();

    public void Bind(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken,
        ZLinkActorRef actorRef)
    {
        lock (_entries)
        {
            _entries[new ZLinkSessionBindingKey(actorId, bindingToken)] = new ZLinkSessionBindingEntry(
                context,
                bindingToken,
                actorRef);
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
            {
                _entries.Remove(key);
            }
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

    public int UpdateAttachedActorRemoteAddress(
        string actorId,
        string routerChannelId,
        RoutingId targetNodeRid,
        ulong expectedActorGeneration,
        ulong newActorGeneration)
    {
        ZLinkActorRef[] actorRefs;
        lock (_entries)
        {
            actorRefs = _entries
                .Where(entry => string.Equals(entry.Key.ActorId, actorId, StringComparison.Ordinal))
                .Select(static entry => entry.Value.ActorRef)
                .ToArray();
        }

        var updated = 0;
        foreach (var actorRef in actorRefs)
        {
            if (actorRef.TryUpdateRemoteAddress(
                    routerChannelId,
                    targetNodeRid,
                    expectedActorGeneration,
                    newActorGeneration))
            {
                updated++;
            }
        }

        return updated;
    }
}
