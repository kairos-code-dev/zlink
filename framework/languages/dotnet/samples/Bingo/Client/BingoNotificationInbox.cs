using System.Collections.Concurrent;

namespace Bingo.Client;

internal sealed class BingoNotificationInbox
{
    private readonly ConcurrentQueue<object> _messages = new();
    private readonly object _gate = new();
    private TaskCompletionSource _arrived = new(TaskCreationOptions.RunContinuationsAsynchronously);

    public IReadOnlyList<TMessage> Snapshot<TMessage>()
    {
        return _messages.OfType<TMessage>().ToArray();
    }

    public void Enqueue(object message)
    {
        _messages.Enqueue(message);
        Signal();
    }

    public Task WaitAsync(CancellationToken cancellationToken)
    {
        lock (_gate)
        {
            return _arrived.Task.WaitAsync(cancellationToken);
        }
    }

    private void Signal()
    {
        TaskCompletionSource arrived;
        lock (_gate)
        {
            arrived = _arrived;
            _arrived = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        }

        arrived.TrySetResult();
    }
}
