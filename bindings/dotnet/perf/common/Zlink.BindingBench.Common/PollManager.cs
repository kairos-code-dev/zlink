using System;
using System.Collections.Generic;
using System.Diagnostics;
using Zlink;

public sealed class PollManager : IDisposable
{
    private PollEvents[] _monitorPollEvents = Array.Empty<PollEvents>();
    private PollEvents[] _monitorRevents = Array.Empty<PollEvents>();
    private PollEvents[] _socketPollEvents = Array.Empty<PollEvents>();
    private PollEvents[] _socketRevents = Array.Empty<PollEvents>();

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
                                        || PerfShared.IsInterrupted(ex.InternalErrno))
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
        try
        {
            return ZlinkPoll.Poll(sockets, eventMasks,
                _socketRevents.AsSpan(0, count), timeoutMs);
        }
        catch (ZlinkException ex) when (PerfShared.IsWouldBlock(ex.InternalErrno)
                                        || PerfShared.IsInterrupted(ex.InternalErrno))
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
            _socketPollEvents[i] = events;

        try
        {
            return ZlinkPoll.Poll(sockets, _socketPollEvents,
                _socketRevents.AsSpan(0, count), timeoutMs);
        }
        catch (ZlinkException ex) when (PerfShared.IsWouldBlock(ex.InternalErrno)
                                        || PerfShared.IsInterrupted(ex.InternalErrno))
        {
            return 0;
        }
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
        if (_socketPollEvents.Length < count)
            _socketPollEvents = new PollEvents[count];
        if (_socketRevents.Length < count)
            _socketRevents = new PollEvents[count];
    }
}
