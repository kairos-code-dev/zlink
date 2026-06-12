using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Streams;

namespace DeliveryDispatch.Server.Session;

internal sealed class CustomerSessionDirectory
{
    private readonly object _gate = new();
    private readonly Dictionary<string, IZLinkSessionContext> _sessions = new(StringComparer.Ordinal);
    private readonly Dictionary<string, HashSet<string>> _subscriptions = new(StringComparer.Ordinal);

    public void Add(IZLinkSessionContext context)
    {
        lock (_gate)
        {
            _sessions[context.SessionId] = context;
        }
    }

    public void Remove(IZLinkSessionContext context)
    {
        lock (_gate)
        {
            _sessions.Remove(context.SessionId);
            foreach (var delivery in _subscriptions.Values)
            {
                delivery.Remove(context.SessionId);
            }
        }
    }

    public void Subscribe(IZLinkSessionContext context, string deliveryId)
    {
        lock (_gate)
        {
            _sessions[context.SessionId] = context;
            if (!_subscriptions.TryGetValue(deliveryId, out var sessions))
            {
                sessions = new HashSet<string>(StringComparer.Ordinal);
                _subscriptions[deliveryId] = sessions;
            }

            sessions.Add(context.SessionId);
        }
    }

    public async ValueTask PublishAsync(
        DeliveryStatusNotify status,
        CancellationToken cancellationToken)
    {
        IZLinkSessionContext[] targets;
        lock (_gate)
        {
            if (!_subscriptions.TryGetValue(status.DeliveryId, out var sessions))
            {
                return;
            }

            targets = sessions
                .Select(sessionId => _sessions.TryGetValue(sessionId, out var context) ? context : null)
                .OfType<IZLinkSessionContext>()
                .ToArray();
        }

        foreach (var target in targets)
        {
            cancellationToken.ThrowIfCancellationRequested();
            await target.Client.Send(status).Async();
        }
    }
}
