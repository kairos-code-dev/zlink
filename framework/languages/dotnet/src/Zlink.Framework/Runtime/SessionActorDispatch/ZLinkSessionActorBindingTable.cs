namespace Zlink.Framework.Runtime.SessionActorDispatch;

internal sealed record ZLinkSessionBindingEntry(
    ZLinkSessionContext Context,
    string BindingToken);

internal sealed class ZLinkSessionActorBindingTable
{
    private readonly Dictionary<string, ZLinkSessionBindingEntry> _entries = new(StringComparer.Ordinal);

    public void Bind(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken)
    {
        lock (_entries)
        {
            _entries[actorId] = new ZLinkSessionBindingEntry(context, bindingToken);
        }
    }

    public void Unbind(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken)
    {
        lock (_entries)
        {
            if (_entries.TryGetValue(actorId, out var existing)
                && ReferenceEquals(existing.Context, context)
                && string.Equals(existing.BindingToken, bindingToken, StringComparison.Ordinal))
            {
                _entries.Remove(actorId);
            }
        }
    }

    public bool TryGet(
        string actorId,
        string sessionId,
        string bindingToken,
        out ZLinkSessionContext context)
    {
        lock (_entries)
        {
            if (_entries.TryGetValue(actorId, out var entry)
                && string.Equals(entry.Context.SessionId, sessionId, StringComparison.Ordinal)
                && string.Equals(entry.BindingToken, bindingToken, StringComparison.Ordinal))
            {
                context = entry.Context;
                return true;
            }

            context = null!;
            return false;
        }
    }
}
