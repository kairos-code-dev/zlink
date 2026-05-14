using System;
using System.Collections.Generic;
using System.Diagnostics;
using Systems.Zlink;

public sealed class MonitorReadyPoller : IDisposable
{
    private readonly List<SocketMonitor> _activeMonitors = new();

    public int Poll(List<SocketMonitor> monitors, int[] activeIndices,
        int activeCount, long deadlineTicks, long nowTicks)
    {
        if (activeCount <= 0)
            return 0;

        long remainingTicks = deadlineTicks - nowTicks;
        if (remainingTicks <= 0)
            return 0;

        long remainingMs = (remainingTicks * 1000L + Stopwatch.Frequency - 1)
            / Stopwatch.Frequency;
        int timeoutMs = remainingMs > int.MaxValue
            ? int.MaxValue
            : (int)remainingMs;

        _activeMonitors.Clear();
        for (int i = 0; i < activeCount; i++)
            _activeMonitors.Add(monitors[activeIndices[i]]);

        try
        {
            return ZlinkPoll.Poll(_activeMonitors, timeoutMs);
        }
        catch (ZlinkException ex) when (PerfShared.IsWouldBlock(ex.InternalErrno)
                                        || PerfShared.IsInterrupted(ex.InternalErrno)
                                        || ex.InternalErrno == 0)
        {
            return 0;
        }
    }

    public void Dispose()
    {
        _activeMonitors.Clear();
    }
}

public sealed class SocketReadyPoller : IDisposable
{
    private PollEventFlags[] _revents = Array.Empty<PollEventFlags>();
    private PollEvent[] _readyEvents = Array.Empty<PollEvent>();
    private int[] _readyIndexes = Array.Empty<int>();
    private Poller? _poller;
    private SocketBase[] _registeredSockets = Array.Empty<SocketBase>();
    private PollEventFlags[] _registeredMasks = Array.Empty<PollEventFlags>();
    private PollEventFlags[] _requestedMasks = Array.Empty<PollEventFlags>();
    private int _activePollerSize;
    private int _readyCount;

    public int Poll(IReadOnlyList<SocketBase> sockets,
        IReadOnlyList<PollEventFlags> eventMasks, int timeoutMs)
    {
        int count = sockets.Count;
        if (count <= 0 || eventMasks.Count < count)
            return 0;

        EnsureCapacity(count);
        Array.Clear(_revents, 0, count);
        _readyCount = 0;
        EnsurePollerState(sockets, eventMasks, count);

        try
        {
            if (_poller == null || _activePollerSize == 0)
                return 0;

            int written = _poller.Wait(_readyEvents.AsSpan(0, count),
                TimeSpan.FromMilliseconds(timeoutMs), out _);
            if (written == 0)
                return 0;

            int ready = 0;
            for (int i = 0; i < written; i++)
            {
                PollEvent readyEvent = _readyEvents[i];
                if (readyEvent.Tag is not int index)
                    continue;
                PollEventFlags revents = readyEvent.Revents;
                if ((uint)index >= (uint)count
                    || _registeredMasks[index] == PollEventFlags.None)
                    continue;
                if (revents == PollEventFlags.None)
                    continue;

                _revents[index] = revents;
                _readyIndexes[ready] = index;
                ready++;
            }

            _readyCount = ready;
            return ready;
        }
        catch (ZlinkException ex) when (PerfShared.IsWouldBlock(ex.InternalErrno)
                                        || PerfShared.IsInterrupted(ex.InternalErrno)
                                        || ex.InternalErrno == 0)
        {
            return 0;
        }
    }

    public int Poll(IReadOnlyList<SocketBase> sockets, PollEventFlags events,
        int timeoutMs)
    {
        int count = sockets.Count;
        if (count <= 0)
            return 0;

        EnsureCapacity(count);
        for (int i = 0; i < count; i++)
            _requestedMasks[i] = events;
        return Poll(sockets, _requestedMasks, timeoutMs);
    }

    public bool IsReadReady(int index)
    {
        return IsReady(index, PollEventFlags.PollIn);
    }

    public bool IsWriteReady(int index)
    {
        return IsReady(index, PollEventFlags.PollOut);
    }

    public int ReadyCount => _readyCount;

    public int ReadyIndexAt(int offset)
    {
        if ((uint)offset >= (uint)_readyCount)
            throw new ArgumentOutOfRangeException(nameof(offset));
        return _readyIndexes[offset];
    }

    public void Dispose()
    {
        _poller?.Dispose();
        _poller = null;
    }

    private bool IsReady(int index, PollEventFlags events)
    {
        return index >= 0
            && index < _revents.Length
            && (_revents[index] & events) != 0;
    }

    private void EnsureCapacity(int count)
    {
        if (_revents.Length < count)
            _revents = new PollEventFlags[count];
        if (_readyEvents.Length < count)
            _readyEvents = new PollEvent[count];
        if (_readyIndexes.Length < count)
            _readyIndexes = new int[count];
        if (_registeredSockets.Length < count)
            _registeredSockets = new SocketBase[count];
        if (_registeredMasks.Length < count)
            _registeredMasks = new PollEventFlags[count];
        if (_requestedMasks.Length < count)
            _requestedMasks = new PollEventFlags[count];
    }

    private void EnsurePollerState(IReadOnlyList<SocketBase> sockets,
        IReadOnlyList<PollEventFlags> eventMasks, int count)
    {
        if (_poller == null || !HasSameLayout(sockets, count))
        {
            RebuildPoller(sockets, eventMasks, count);
            return;
        }

        for (int i = 0; i < count; i++)
        {
            PollEventFlags previous = _registeredMasks[i];
            PollEventFlags current = eventMasks[i];
            if (previous == current)
                continue;

            if (previous == PollEventFlags.None)
            {
                if (current != PollEventFlags.None)
                {
                    _poller.Add(sockets[i], current, i);
                    _activePollerSize++;
                }
            }
            else if (current == PollEventFlags.None)
            {
                if (_poller.Remove(sockets[i]))
                    _activePollerSize--;
            }
            else
            {
                _poller.Modify(sockets[i], current);
            }

            _registeredMasks[i] = current;
        }
    }

    private bool HasSameLayout(IReadOnlyList<SocketBase> sockets, int count)
    {
        for (int i = 0; i < count; i++)
        {
            if (!ReferenceEquals(_registeredSockets[i], sockets[i]))
                return false;
        }

        return true;
    }

    private void RebuildPoller(IReadOnlyList<SocketBase> sockets,
        IReadOnlyList<PollEventFlags> eventMasks, int count)
    {
        _poller?.Dispose();
        _poller = new Poller();
        _activePollerSize = 0;
        _readyCount = 0;

        for (int i = 0; i < count; i++)
        {
            _registeredSockets[i] = sockets[i];
            PollEventFlags mask = eventMasks[i];
            _registeredMasks[i] = mask;
            if (mask != PollEventFlags.None)
            {
                _poller.Add(sockets[i], mask, i);
                _activePollerSize++;
            }
        }
    }
}

public sealed class PollManager : IDisposable
{
    private readonly MonitorReadyPoller _monitorPoller = new();
    private readonly SocketReadyPoller _socketPoller = new();

    public int PollMonitors(List<SocketMonitor> monitors, int[] activeIndices,
        int activeCount, long deadlineTicks, long nowTicks)
    {
        return _monitorPoller.Poll(monitors, activeIndices, activeCount,
            deadlineTicks, nowTicks);
    }

    public int PollSockets(IReadOnlyList<SocketBase> sockets,
        IReadOnlyList<PollEventFlags> eventMasks, int timeoutMs)
    {
        return _socketPoller.Poll(sockets, eventMasks, timeoutMs);
    }

    public int PollSockets(IReadOnlyList<SocketBase> sockets, PollEventFlags events,
        int timeoutMs)
    {
        return _socketPoller.Poll(sockets, events, timeoutMs);
    }

    public bool IsSocketReadReady(int index)
    {
        return _socketPoller.IsReadReady(index);
    }

    public bool IsSocketWriteReady(int index)
    {
        return _socketPoller.IsWriteReady(index);
    }

    public int ReadySocketCount => _socketPoller.ReadyCount;

    public int ReadySocketIndexAt(int offset)
    {
        return _socketPoller.ReadyIndexAt(offset);
    }

    public void Dispose()
    {
        _socketPoller.Dispose();
        _monitorPoller.Dispose();
    }
}
