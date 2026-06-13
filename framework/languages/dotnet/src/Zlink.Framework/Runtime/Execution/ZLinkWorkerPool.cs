namespace Zlink.Framework.Runtime.Execution;

/// <summary>
/// Single elastic bounded worker pool. Threads are spawned on demand up to
/// <see cref="MaxThreads"/>, exit after <see cref="_idleTimeout"/> of
/// inactivity, and queued work is bounded by <see cref="_maxQueueLength"/>.
/// A full queue fails the submit immediately; the pool never blocks the
/// submitting dispatcher and never runs work on the caller thread.
/// </summary>
internal sealed class ZLinkWorkerPool : IDisposable
{
    private readonly object _sync = new();
    private readonly Queue<Action<CancellationToken>> _queue = new();
    private readonly CancellationTokenSource _shutdownSource = new();
    private readonly int _minThreads;
    private readonly int _maxThreads;
    private readonly TimeSpan _idleTimeout;
    private readonly int _maxQueueLength;
    private int _threadCount;
    private int _idleThreads;
    private bool _disposed;

    public ZLinkWorkerPool(
        int minThreads,
        int maxThreads,
        TimeSpan idleTimeout,
        int maxQueueLength)
    {
        if (minThreads < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(minThreads));
        }

        if (maxThreads < Math.Max(1, minThreads))
        {
            throw new ArgumentOutOfRangeException(nameof(maxThreads));
        }

        if (idleTimeout <= TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(idleTimeout));
        }

        if (maxQueueLength < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(maxQueueLength));
        }

        _minThreads = minThreads;
        _maxThreads = maxThreads;
        _idleTimeout = idleTimeout;
        _maxQueueLength = maxQueueLength;
    }

    public CancellationToken ShutdownToken => _shutdownSource.Token;

    public int MaxThreads => _maxThreads;

    public int ThreadCount
    {
        get
        {
            lock (_sync)
            {
                return _threadCount;
            }
        }
    }

    public int QueueLength
    {
        get
        {
            lock (_sync)
            {
                return _queue.Count;
            }
        }
    }

    public bool TrySubmit(Action<CancellationToken> work)
    {
        lock (_sync)
        {
            if (_disposed || _queue.Count >= _maxQueueLength)
            {
                return false;
            }

            _queue.Enqueue(work);
            if (_idleThreads > 0)
            {
                Monitor.Pulse(_sync);
            }
            else if (_threadCount < _maxThreads)
            {
                _threadCount++;
                StartWorkerThread();
            }

            return true;
        }
    }

    public void Dispose()
    {
        lock (_sync)
        {
            if (_disposed)
            {
                return;
            }

            _disposed = true;
            _queue.Clear();
            Monitor.PulseAll(_sync);
        }

        _shutdownSource.Cancel();
    }

    private void StartWorkerThread()
    {
        var thread = new Thread(WorkerLoop)
        {
            IsBackground = true,
            Name = "zlink-worker",
        };
        thread.Start();
    }

    private void WorkerLoop()
    {
        while (true)
        {
            Action<CancellationToken> work;
            lock (_sync)
            {
                while (_queue.Count == 0)
                {
                    if (_disposed)
                    {
                        _threadCount--;
                        return;
                    }

                    _idleThreads++;
                    var signaled = Monitor.Wait(_sync, _idleTimeout);
                    _idleThreads--;
                    if (!signaled && _queue.Count == 0 && _threadCount > _minThreads)
                    {
                        _threadCount--;
                        return;
                    }
                }

                work = _queue.Dequeue();
            }

            try
            {
                work(_shutdownSource.Token);
            }
            catch
            {
                // Worker call wrappers convert their own failures; a throwing
                // wrapper must never take the pool thread down.
            }
        }
    }
}
