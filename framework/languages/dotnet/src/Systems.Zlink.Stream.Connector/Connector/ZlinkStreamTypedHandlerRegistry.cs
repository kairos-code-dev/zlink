namespace Systems.Zlink.Stream.Connector.Connector;

internal sealed class ZlinkStreamTypedHandlerRegistry
{
    private readonly Dictionary<string, List<TypedHandler>> _handlers = new(StringComparer.Ordinal);
    private readonly object _gate = new();

    public IDisposable Add<TBody>(
        string name,
        Func<ZlinkStreamMessage<TBody>, CancellationToken, ValueTask> handler)
    {
        var typed = new TypedHandler(typeof(TBody), async (message, body, cancellationToken) =>
        {
            await handler(new ZlinkStreamMessage<TBody>(message.Name, message.Metadata, (TBody)body!), cancellationToken)
                .ConfigureAwait(false);
        });

        lock (_gate)
        {
            if (!_handlers.TryGetValue(name, out var list))
            {
                list = [];
                _handlers.Add(name, list);
            }

            list.Add(typed);
        }

        return new Subscription(() => Remove(name, typed));
    }

    public IReadOnlyList<TypedHandler> Snapshot(string packetName)
    {
        lock (_gate)
        {
            return _handlers.TryGetValue(packetName, out var list)
                ? list.ToArray()
                : [];
        }
    }

    private void Remove(string name, TypedHandler typed)
    {
        lock (_gate)
        {
            if (_handlers.TryGetValue(name, out var list))
            {
                list.Remove(typed);
                if (list.Count == 0)
                {
                    _handlers.Remove(name);
                }
            }
        }
    }

    internal sealed record TypedHandler(
        Type BodyType,
        Func<ZlinkStreamMessage, object?, CancellationToken, ValueTask> Invoke);

    private sealed class Subscription(Action dispose) : IDisposable
    {
        private int _disposed;

        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) == 0)
            {
                dispose();
            }
        }
    }
}
