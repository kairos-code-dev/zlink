namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorDispatchMailbox
{
    private readonly object _sync = new();
    private readonly Queue<Waiter> _waiters = new();
    private bool _busy;
    private int _pendingMessages;

    public int PendingCount
    {
        get
        {
            lock (_sync) return _pendingMessages;
        }
    }

    public ValueTask<Turn> EnterAsync(
        CancellationToken cancellationToken,
        bool countAsPendingMessage = false)
    {
        cancellationToken.ThrowIfCancellationRequested();

        lock (_sync)
        {
            if (!_busy)
            {
                _busy = true;
                return ValueTask.FromResult(new Turn(this));
            }

            var waiter = new Waiter(cancellationToken, countAsPendingMessage);
            _waiters.Enqueue(waiter);
            if (countAsPendingMessage)
            {
                _pendingMessages++;
                ZLinkRuntimeMetrics.RecordActorMailboxEnqueued();
            }
            return AwaitTurnAsync(waiter);
        }
    }

    private static async ValueTask<Turn> AwaitTurnAsync(Waiter waiter)
    {
        try
        {
            await waiter.Task.ConfigureAwait(false);
            return new Turn(waiter.Owner!);
        }
        finally
        {
            waiter.Dispose();
        }
    }

    private void Release()
    {
        while (true)
        {
            Waiter? next = null;
            lock (_sync)
            {
                while (_waiters.Count > 0)
                {
                    next = _waiters.Dequeue();
                    if (next.CountsAsPendingMessage)
                    {
                        _pendingMessages--;
                        ZLinkRuntimeMetrics.RecordActorMailboxStarted();
                    }
                    if (!next.IsCanceled)
                    {
                        next.Owner = this;
                        break;
                    }

                    next.Dispose();
                    next = null;
                }

                if (next is null)
                {
                    _busy = false;
                    return;
                }
            }

            if (next.TrySetReady()) return;

            next.Dispose();
        }
    }

    public readonly struct Turn : IDisposable
    {
        private readonly ZLinkActorDispatchMailbox? _mailbox;

        internal Turn(ZLinkActorDispatchMailbox mailbox)
        {
            _mailbox = mailbox;
        }

        public void Dispose()
        {
            _mailbox?.Release();
        }
    }

    private sealed class Waiter : IDisposable
    {
        private readonly TaskCompletionSource _ready =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        private readonly CancellationTokenRegistration _registration;
        private int _canceled;

        public Waiter(
            CancellationToken cancellationToken,
            bool countsAsPendingMessage)
        {
            CountsAsPendingMessage = countsAsPendingMessage;
            if (cancellationToken.CanBeCanceled)
                _registration = cancellationToken.Register(
                    static state => ((Waiter)state!).Cancel(),
                    this);
        }

        public ZLinkActorDispatchMailbox? Owner { get; set; }

        public Task Task => _ready.Task;

        public bool IsCanceled => Volatile.Read(ref _canceled) != 0;

        public bool CountsAsPendingMessage { get; }

        public void Dispose()
        {
            _registration.Dispose();
        }

        public bool TrySetReady()
        {
            if (IsCanceled) return false;

            Dispose();
            return _ready.TrySetResult();
        }

        private void Cancel()
        {
            if (Interlocked.Exchange(ref _canceled, 1) == 0) _ready.TrySetException(new OperationCanceledException());
        }
    }
}
