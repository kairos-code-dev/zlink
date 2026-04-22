using System;
using System.Collections.Generic;
using System.Diagnostics;
using Zlink;

public sealed class MonitorReadyPoller : IDisposable
{
    private PollEvents[] _pollEvents = Array.Empty<PollEvents>();
    private PollEvents[] _revents = Array.Empty<PollEvents>();

    public int Poll(List<SocketMonitor> monitors, int[] activeIndices,
        int activeCount, long deadlineTicks, long nowTicks)
    {
        if (activeCount <= 0)
            return 0;

        long remainingMs = (deadlineTicks - nowTicks) * 1000L
            / Stopwatch.Frequency;
        EnsureCapacity(activeCount);

        var pollMonitors = new List<SocketMonitor>(activeCount);
        for (int i = 0; i < activeCount; i++)
        {
            pollMonitors.Add(monitors[activeIndices[i]]);
            _pollEvents[i] = PollEvents.PollIn;
            _revents[i] = PollEvents.None;
        }

        try
        {
            return ZlinkPoll.Poll(pollMonitors, _pollEvents,
                _revents.AsSpan(0, activeCount),
                (int)Math.Max(0, remainingMs));
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
    }

    private void EnsureCapacity(int count)
    {
        if (_pollEvents.Length < count)
            _pollEvents = new PollEvents[count];
        if (_revents.Length < count)
            _revents = new PollEvents[count];
    }
}

public sealed class SocketReadyPoller : IDisposable
{
    private PollEvents[] _revents = Array.Empty<PollEvents>();
    private Poller? _poller;
    private SocketBase[] _registeredSockets = Array.Empty<SocketBase>();
    private PollEvents[] _registeredMasks = Array.Empty<PollEvents>();
    private PollEvent[] _events = Array.Empty<PollEvent>();

    public int Poll(IReadOnlyList<SocketBase> sockets,
        IReadOnlyList<PollEvents> eventMasks, int timeoutMs)
    {
        int count = sockets.Count;
        if (count <= 0 || eventMasks.Count < count)
            return 0;

        EnsureCapacity(count);
        Array.Clear(_revents, 0, count);
        EnsurePollerState(sockets, eventMasks, count);

        try
        {
            if (_poller == null || _poller.Count == 0)
                return 0;

            int written = _poller.Wait(_events, timeoutMs, out int totalReady);
            if (written == 0 && totalReady == 0)
                return 0;

            int ready = 0;
            for (int i = 0; i < written; i++)
            {
                if (_events[i].Socket is not SocketBase socket)
                    continue;

                int index = FindSocketIndex(socket, count);
                if (index < 0)
                    continue;

                _revents[index] = _events[i].Revents;
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

    public int Poll(IReadOnlyList<SocketBase> sockets, PollEvents events,
        int timeoutMs)
    {
        int count = sockets.Count;
        if (count <= 0)
            return 0;

        EnsureCapacity(count);
        for (int i = 0; i < count; i++)
            _registeredMasks[i] = events;
        return Poll(sockets, _registeredMasks.AsSpan(0, count).ToArray(),
            timeoutMs);
    }

    public bool IsReadReady(int index)
    {
        return IsReady(index, PollEvents.PollIn);
    }

    public bool IsWriteReady(int index)
    {
        return IsReady(index, PollEvents.PollOut);
    }

    public void Dispose()
    {
        _poller?.Dispose();
        _poller = null;
    }

    private bool IsReady(int index, PollEvents events)
    {
        return index >= 0
            && index < _revents.Length
            && (_revents[index] & events) != 0;
    }

    private void EnsureCapacity(int count)
    {
        if (_revents.Length < count)
            _revents = new PollEvents[count];
        if (_registeredSockets.Length < count)
            _registeredSockets = new SocketBase[count];
        if (_registeredMasks.Length < count)
            _registeredMasks = new PollEvents[count];
        if (_events.Length < count)
            _events = new PollEvent[count];
    }

    private void EnsurePollerState(IReadOnlyList<SocketBase> sockets,
        IReadOnlyList<PollEvents> eventMasks, int count)
    {
        if (_poller == null || !HasSameLayout(sockets, count))
        {
            RebuildPoller(sockets, eventMasks, count);
            return;
        }

        for (int i = 0; i < count; i++)
        {
            PollEvents previous = _registeredMasks[i];
            PollEvents current = eventMasks[i];
            if (previous == current)
                continue;

            if (previous == PollEvents.None)
            {
                if (current != PollEvents.None)
                    _poller.Add(sockets[i], current);
            }
            else if (current == PollEvents.None)
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
        IReadOnlyList<PollEvents> eventMasks, int count)
    {
        _poller?.Dispose();
        _poller = new Poller();

        for (int i = 0; i < count; i++)
        {
            _registeredSockets[i] = sockets[i];
            PollEvents mask = eventMasks[i];
            _registeredMasks[i] = mask;
            if (mask != PollEvents.None)
                _poller.Add(sockets[i], mask);
        }
    }

    private int FindSocketIndex(SocketBase socket, int count)
    {
        for (int i = 0; i < count; i++)
        {
            if (ReferenceEquals(_registeredSockets[i], socket))
                return i;
        }

        return -1;
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
        IReadOnlyList<PollEvents> eventMasks, int timeoutMs)
    {
        return _socketPoller.Poll(sockets, eventMasks, timeoutMs);
    }

    public int PollSockets(IReadOnlyList<SocketBase> sockets, PollEvents events,
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
