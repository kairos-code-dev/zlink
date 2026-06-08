namespace Systems.Zlink.Stream.Connector.Runtime;

internal sealed class ZlinkStreamReceivedMessages
{
    private readonly Dictionary<string, List<ReceivedMessage>> _messages = new(StringComparer.Ordinal);
    private readonly object _gate = new();
    private TaskCompletionSource _arrived = new(TaskCreationOptions.RunContinuationsAsynchronously);

    public int Count(string name)
    {
        lock (_gate)
        {
            return _messages.TryGetValue(name, out var messages) ? messages.Count : 0;
        }
    }

    public void Record(ZlinkStreamMessage<ZlinkStreamEncodedPayload> message)
    {
        TaskCompletionSource arrived;
        lock (_gate)
        {
            if (!_messages.TryGetValue(message.Name, out var messages))
            {
                messages = [];
                _messages.Add(message.Name, messages);
            }

            messages.Add(new ReceivedMessage(message));
            arrived = _arrived;
            _arrived = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        }

        arrived.TrySetResult();
    }

    public async ValueTask<ZlinkStreamMessage<ZlinkStreamEncodedPayload>> WaitForAsync(
        string name,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await WaitForAsync(
                name,
                static _ => true,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZlinkStreamMessage<ZlinkStreamEncodedPayload>> WaitForAsync(
        string name,
        Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, bool> predicate,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(timeout);

        while (!timeoutSource.IsCancellationRequested)
        {
            var message = TryTake(name, predicate);
            if (message is not null)
            {
                return message;
            }

            try
            {
                await WaitAsync(timeoutSource.Token).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (timeoutSource.IsCancellationRequested
                                                     && !cancellationToken.IsCancellationRequested)
            {
                break;
            }
        }

        throw new TimeoutException($"Timed out waiting for '{name}' stream message.");
    }

    private ZlinkStreamMessage<ZlinkStreamEncodedPayload>? TryTake(
        string name,
        Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, bool> predicate)
    {
        lock (_gate)
        {
            if (!_messages.TryGetValue(name, out var messages))
            {
                return null;
            }

            foreach (var message in messages)
            {
                if (message.Consumed || !predicate(message.Value))
                {
                    continue;
                }

                message.Consumed = true;
                return message.Value;
            }

            return null;
        }
    }

    private Task WaitAsync(CancellationToken cancellationToken)
    {
        lock (_gate)
        {
            return _arrived.Task.WaitAsync(cancellationToken);
        }
    }

    private sealed class ReceivedMessage(
        ZlinkStreamMessage<ZlinkStreamEncodedPayload> value)
    {
        public ZlinkStreamMessage<ZlinkStreamEncodedPayload> Value { get; } = value;

        public bool Consumed { get; set; }
    }
}
