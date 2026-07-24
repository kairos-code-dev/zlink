namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorDispatchMailbox
{
    private readonly object _sync = new();
    private readonly Queue<Waiter> _barrierWaiters = new();
    private readonly Queue<Waiter> _waiters = new();
    private bool _busy;
    private int _pendingMessages;
    private int _pendingRequests;

    public int PendingRequestCount
    {
        get
        {
            lock (_sync) return _pendingRequests;
        }
    }

    public ValueTask<Turn> EnterAsync(
        CancellationToken cancellationToken,
        bool countAsPendingMessage = false,
        bool countAsPendingRequest = false)
    {
        cancellationToken.ThrowIfCancellationRequested();

        lock (_sync)
        {
            if (!_busy)
            {
                _busy = true;
                return ValueTask.FromResult(new Turn(this));
            }

            var waiter = new Waiter(
                cancellationToken,
                countAsPendingMessage,
                countAsPendingRequest);
            _waiters.Enqueue(waiter);
            if (countAsPendingMessage)
            {
                _pendingMessages++;
                ZLinkRuntimeMetrics.RecordActorMailboxEnqueued();
            }
            if (countAsPendingRequest) _pendingRequests++;
            return AwaitTurnAsync(waiter);
        }
    }

    public BarrierReservation ReserveBarrier()
    {
        var cancellation = new CancellationTokenSource();
        ValueTask<Turn> turn;
        lock (_sync)
        {
            if (!_busy)
            {
                _busy = true;
                turn = ValueTask.FromResult(new Turn(this));
            }
            else
            {
                var waiter = new Waiter(cancellation.Token, false, false);
                _barrierWaiters.Enqueue(waiter);
                turn = AwaitTurnAsync(waiter);
            }
        }

        return new BarrierReservation(turn.AsTask(), cancellation);
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
                while (_barrierWaiters.Count > 0 || _waiters.Count > 0)
                {
                    next = _barrierWaiters.Count > 0
                        ? _barrierWaiters.Dequeue()
                        : _waiters.Dequeue();
                    if (next.CountsAsPendingMessage)
                    {
                        _pendingMessages--;
                        ZLinkRuntimeMetrics.RecordActorMailboxStarted();
                    }
                    if (next.CountsAsPendingRequest) _pendingRequests--;
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

    public sealed class BarrierReservation(
        Task<Turn> turn,
        CancellationTokenSource cancellation)
    {
        private int _claimed;

        public async ValueTask<Turn> ClaimAsync()
        {
            if (Interlocked.Exchange(ref _claimed, 1) != 0)
                throw new InvalidOperationException("Actor barrier reservation was already consumed.");

            cancellation.Dispose();
            return await turn.ConfigureAwait(false);
        }

        public void Discard()
        {
            if (Interlocked.Exchange(ref _claimed, 1) != 0) return;

            if (turn.IsCompletedSuccessfully)
            {
                cancellation.Dispose();
                turn.Result.Dispose();
                return;
            }

            cancellation.Cancel();
            cancellation.Dispose();
            _ = ObserveCancellationAsync(turn);
        }

        private static async Task ObserveCancellationAsync(Task<Turn> pending)
        {
            try
            {
                using var turn = await pending.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
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
            bool countsAsPendingMessage,
            bool countsAsPendingRequest)
        {
            CountsAsPendingMessage = countsAsPendingMessage;
            CountsAsPendingRequest = countsAsPendingRequest;
            if (cancellationToken.CanBeCanceled)
                _registration = cancellationToken.Register(
                    static state => ((Waiter)state!).Cancel(),
                    this);
        }

        public ZLinkActorDispatchMailbox? Owner { get; set; }

        public Task Task => _ready.Task;

        public bool IsCanceled => Volatile.Read(ref _canceled) != 0;

        public bool CountsAsPendingMessage { get; }

        public bool CountsAsPendingRequest { get; }

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
