using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Systems.Zlink;

public sealed class MonitorReadyPoller : IDisposable
{
    public int Poll(List<SocketMonitor> monitors, int[] activeIndices,
        int activeCount, long deadlineTicks, long nowTicks)
    {
        if (activeCount <= 0)
            return 0;

        int ready = CountReady(monitors, activeIndices, activeCount);
        if (ready > 0)
            return ready;

        long remainingMs = Math.Max(0, (deadlineTicks - nowTicks) * 1000L
            / Stopwatch.Frequency);
        if (remainingMs > 0)
        {
            Thread.Sleep((int)Math.Min(remainingMs, 1));
            ready = CountReady(monitors, activeIndices, activeCount);
        }

        return ready;
    }

    public void Dispose()
    {
    }

    private static int CountReady(List<SocketMonitor> monitors,
        int[] activeIndices, int activeCount)
    {
        int ready = 0;
        for (int i = 0; i < activeCount; i++)
        {
            try
            {
                if (monitors[activeIndices[i]].Snapshot().IsReady)
                    ready++;
            }
            catch (ZlinkException ex) when (PerfShared.IsWouldBlock(ex.InternalErrno)
                                            || PerfShared.IsInterrupted(ex.InternalErrno)
                                            || ex.InternalErrno == 0)
            {
            }
        }

        return ready;
    }
}

public sealed class SocketReadyPoller : IDisposable
{
    private PollEventFlags[] _revents = Array.Empty<PollEventFlags>();
    private Poller? _poller;
    private SocketBase[] _registeredSockets = Array.Empty<SocketBase>();
    private PollEventFlags[] _registeredMasks = Array.Empty<PollEventFlags>();
    private PollEventFlags[] _requestedMasks = Array.Empty<PollEventFlags>();
    private readonly Dictionary<SocketBase, int> _socketIndexes =
        new(SocketReferenceComparer.Instance);

    public int Poll(IReadOnlyList<SocketBase> sockets,
        IReadOnlyList<PollEventFlags> eventMasks, int timeoutMs)
    {
        int count = sockets.Count;
        if (count <= 0 || eventMasks.Count < count)
            return 0;

        EnsureCapacity(count);
        Array.Clear(_revents, 0, count);
        EnsurePollerState(sockets, eventMasks, count);

        try
        {
            if (_poller == null || _poller.Size == 0)
                return 0;

            IReadOnlyList<PollEvent> events = _poller.WaitAll(count,
                TimeSpan.FromMilliseconds(timeoutMs));
            if (events.Count == 0)
                return 0;

            int ready = 0;
            for (int i = 0; i < events.Count; i++)
            {
                if (events[i].Socket is not SocketBase socket)
                    continue;

                int index = FindSocketIndex(socket, count);
                if (index < 0)
                    continue;

                _revents[index] = events[i].Revents;
                ready++;
            }

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
                    _poller.Add(sockets[i], current);
            }
            else if (current == PollEventFlags.None)
            {
                _poller.Remove(sockets[i]);
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
        _socketIndexes.Clear();

        for (int i = 0; i < count; i++)
        {
            _registeredSockets[i] = sockets[i];
            _socketIndexes[sockets[i]] = i;
            PollEventFlags mask = eventMasks[i];
            _registeredMasks[i] = mask;
            if (mask != PollEventFlags.None)
                _poller.Add(sockets[i], mask);
        }
    }

    private int FindSocketIndex(SocketBase socket, int count)
    {
        return _socketIndexes.TryGetValue(socket, out int index) && index < count
            ? index
            : -1;
    }

    private sealed class SocketReferenceComparer : IEqualityComparer<SocketBase>
    {
        internal static readonly SocketReferenceComparer Instance = new();

        public bool Equals(SocketBase? x, SocketBase? y)
        {
            return ReferenceEquals(x, y);
        }

        public int GetHashCode(SocketBase obj)
        {
            return System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(obj);
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

    public void Dispose()
    {
        _socketPoller.Dispose();
        _monitorPoller.Dispose();
    }
}
