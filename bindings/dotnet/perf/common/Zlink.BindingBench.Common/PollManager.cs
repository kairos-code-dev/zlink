using System;
using System.Collections.Generic;
using System.Diagnostics;
using Zlink;

public sealed class PollManager : IDisposable
{
    private PollEvents[] _monitorPollEvents = Array.Empty<PollEvents>();
    private PollEvents[] _monitorRevents = Array.Empty<PollEvents>();
    private PollEvents[] _socketRevents = Array.Empty<PollEvents>();
    private Poller? _socketPoller;
    private SocketBase[] _registeredSockets = Array.Empty<SocketBase>();
    private PollEvents[] _registeredMasks = Array.Empty<PollEvents>();
    private PollEvent[] _socketEvents = Array.Empty<PollEvent>();

    public int PollMonitors(List<Zlink.SocketMonitor> monitors, int[] activeIndices,
        int activeCount, long deadlineTicks, long nowTicks)
    {
        if (activeCount <= 0)
            return 0;

        long remainingMs = (deadlineTicks - nowTicks) * 1000L
            / Stopwatch.Frequency;
        EnsureMonitorPollCapacity(activeCount);

        var pollMonitors = new List<Zlink.SocketMonitor>(activeCount);
        for (int i = 0; i < activeCount; i++)
        {
            pollMonitors.Add(monitors[activeIndices[i]]);
            _monitorPollEvents[i] = PollEvents.PollIn;
            _monitorRevents[i] = PollEvents.None;
        }

        try
        {
            return ZlinkPoll.Poll(pollMonitors, _monitorPollEvents,
                _monitorRevents.AsSpan(0, activeCount),
                (int)Math.Max(0, remainingMs));
        }
        catch (ZlinkException ex) when (PerfShared.IsWouldBlock(ex.InternalErrno)
                                        || PerfShared.IsInterrupted(ex.InternalErrno)
                                        || ex.InternalErrno == 0)
        {
            return 0;
        }
    }

    public int PollSockets(IReadOnlyList<SocketBase> sockets,
        IReadOnlyList<PollEvents> eventMasks, int timeoutMs)
    {
        int count = sockets.Count;
        if (count <= 0 || eventMasks.Count < count)
            return 0;

        EnsureSocketPollCapacity(count);
        Array.Clear(_socketRevents, 0, count);
        EnsureSocketPollerState(sockets, eventMasks, count);

        try
        {
            if (_socketPoller == null || _socketPoller.Count == 0)
                return 0;

            int written = _socketPoller.Wait(_socketEvents, timeoutMs,
                out int totalReady);
            if (written == 0 && totalReady == 0)
                return 0;

            int ready = 0;
            for (int i = 0; i < written; i++)
            {
                IZlinkSocket? socket = _socketEvents[i].Socket;
                if (socket is not SocketBase socketBase)
                    continue;

                int index = FindRegisteredSocketIndex(socketBase, count);
                if (index < 0)
                    continue;

                _socketRevents[index] = _socketEvents[i].Revents;
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

    public int PollSockets(IReadOnlyList<SocketBase> sockets,
        PollEvents events, int timeoutMs)
    {
        int count = sockets.Count;
        if (count <= 0)
            return 0;

        EnsureSocketPollCapacity(count);
        for (int i = 0; i < count; i++)
            _registeredMasks[i] = events;
        return PollSockets(sockets, _registeredMasks.AsSpan(0, count).ToArray(),
            timeoutMs);
    }

    public bool IsSocketReadReady(int index)
    {
        return IsSocketReady(index, PollEvents.PollIn);
    }

    public bool IsSocketWriteReady(int index)
    {
        return IsSocketReady(index, PollEvents.PollOut);
    }

    public void Dispose()
    {
        _socketPoller?.Dispose();
        _socketPoller = null;
    }

    private bool IsSocketReady(int index, PollEvents events)
    {
        return index >= 0
            && index < _socketRevents.Length
            && (_socketRevents[index] & events) != 0;
    }

    private void EnsureMonitorPollCapacity(int count)
    {
        if (_monitorPollEvents.Length < count)
            _monitorPollEvents = new PollEvents[count];
        if (_monitorRevents.Length < count)
            _monitorRevents = new PollEvents[count];
    }

    private void EnsureSocketPollCapacity(int count)
    {
        if (_socketRevents.Length < count)
            _socketRevents = new PollEvents[count];
        if (_registeredSockets.Length < count)
            _registeredSockets = new SocketBase[count];
        if (_registeredMasks.Length < count)
            _registeredMasks = new PollEvents[count];
        if (_socketEvents.Length < count)
            _socketEvents = new PollEvent[count];
    }

    private void EnsureSocketPollerState(IReadOnlyList<SocketBase> sockets,
        IReadOnlyList<PollEvents> eventMasks, int count)
    {
        if (_socketPoller == null || !HasSameSocketLayout(sockets, count))
        {
            RebuildSocketPoller(sockets, eventMasks, count);
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
                    _socketPoller.Add(sockets[i], current);
            }
            else if (current == PollEvents.None)
            {
                _socketPoller.Remove(sockets[i]);
            }
            else
            {
                _socketPoller.Modify(sockets[i], current);
            }

            _registeredMasks[i] = current;
        }
    }

    private bool HasSameSocketLayout(IReadOnlyList<SocketBase> sockets, int count)
    {
        for (int i = 0; i < count; i++)
        {
            if (!ReferenceEquals(_registeredSockets[i], sockets[i]))
                return false;
        }
        return true;
    }

    private void RebuildSocketPoller(IReadOnlyList<SocketBase> sockets,
        IReadOnlyList<PollEvents> eventMasks, int count)
    {
        _socketPoller?.Dispose();
        _socketPoller = new Poller();

        for (int i = 0; i < count; i++)
        {
            _registeredSockets[i] = sockets[i];
            PollEvents mask = eventMasks[i];
            _registeredMasks[i] = mask;
            if (mask != PollEvents.None)
                _socketPoller.Add(sockets[i], mask);
        }
    }

    private int FindRegisteredSocketIndex(SocketBase socket, int count)
    {
        for (int i = 0; i < count; i++)
        {
            if (ReferenceEquals(_registeredSockets[i], socket))
                return i;
        }
        return -1;
    }
}
