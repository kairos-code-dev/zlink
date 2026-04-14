// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;

namespace Zlink;

public sealed class Timer : IDisposable, IAsyncDisposable
{
    private readonly ConcurrentQueue<ulong> _pending = new();
    private readonly AutoResetEvent _signal = new(false);
    private readonly object _gate = new();
    private System.Threading.Timer? _timer;
    private Action<Timer, ulong>? _handler;
    private ulong _fired;
    private ulong _remaining;
    private bool _stopped = true;
    private bool _disposed;
    private Spot? _spot;

    public Timer()
    {
    }

    private Timer(Spot spot)
    {
        _spot = spot ?? throw new ArgumentNullException(nameof(spot));
    }

    public static Timer FromSpot(Spot spot)
    {
        return new Timer(spot);
    }

    public void Start(ulong intervalNs, ulong repeatCount)
    {
        if (intervalNs == 0)
            throw new ArgumentOutOfRangeException(nameof(intervalNs));
        EnsureNotDisposed();

        lock (_gate)
        {
            StopCore();
            _remaining = repeatCount;
            _stopped = false;
            TimeSpan dueTime = TimeSpan.FromMilliseconds(Math.Max(1.0,
                Math.Ceiling(intervalNs / 1_000_000.0)));
            _timer = new System.Threading.Timer(OnTick, null, dueTime, dueTime);
        }
    }

    public void Stop()
    {
        EnsureNotDisposed();
        lock (_gate)
        {
            StopCore();
        }
    }

    public ulong Recv(int flags = 0)
    {
        EnsureNotDisposed();
        if ((flags & 1) != 0)
        {
            if (_pending.TryDequeue(out ulong value))
                return value;
            throw new ZlinkRecvException(RecvResult.NoData);
        }

        while (true)
        {
            if (_pending.TryDequeue(out ulong value))
                return value;
            _signal.WaitOne();
        }
    }

    public void OnFire(Action<Timer, ulong> handler)
    {
        EnsureNotDisposed();
        _handler = handler ?? throw new ArgumentNullException(nameof(handler));
    }

    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        if (_disposed)
            return;

        lock (_gate)
        {
            StopCore();
            _disposed = true;
            _spot = null;
        }

        _signal.Dispose();
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~Timer()
    {
        Dispose();
    }

    private void OnTick(object? state)
    {
        if (_disposed)
            return;

        ulong fired = unchecked(++_fired);
        _pending.Enqueue(fired);
        _signal.Set();

        Action<Timer, ulong>? handler = _handler;
        if (handler != null)
        {
            try
            {
                handler(this, fired);
            }
            catch (Exception ex)
            {
                Runtime.ReportUnhandledCallbackException(ex);
            }
        }

        lock (_gate)
        {
            if (_remaining == 0)
                return;
            _remaining--;
            if (_remaining == 0)
                StopCore();
        }
    }

    private void StopCore()
    {
        _stopped = true;
        _timer?.Dispose();
        _timer = null;
    }

    private void EnsureNotDisposed()
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(Timer));
    }
}
