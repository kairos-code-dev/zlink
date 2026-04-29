namespace Systems.Zlink.Stream.Connector.Runtime;

internal sealed class ZlinkStreamTypedHandlerRegistry
{
    private readonly Dictionary<string, List<TypedHandler>> _handlers = new(StringComparer.Ordinal);
    private readonly object _gate = new();

    public IDisposable Add(
        string name,
        Func<ZlinkStreamMessage<ZlinkStreamEncodedBody>, CancellationToken, ValueTask> handler)
    {
        var typed = new TypedHandler(async (message, body, cancellationToken) =>
        {
            await handler(new ZlinkStreamMessage<ZlinkStreamEncodedBody>(message.Name, message.Metadata, (ZlinkStreamEncodedBody)body!), cancellationToken)
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

    public IReadOnlyList<TypedHandler> Snapshot(string messageName)
    {
        lock (_gate)
        {
            return _handlers.TryGetValue(messageName, out var list)
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
