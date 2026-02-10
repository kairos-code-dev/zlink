// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Zlink.Native;

namespace Zlink;

public sealed class Poller
{
    private readonly List<PollItem> _items = new();

    public void Add(Socket socket, PollEvents events, object? tag = null)
    {
        if (socket == null)
            throw new ArgumentNullException(nameof(socket));
        _items.Add(new PollItem(socket, 0, events, tag));
    }

    public void AddFd(int fd, PollEvents events, object? tag = null)
    {
        _items.Add(new PollItem(null, fd, events, tag));
    }

    public bool Remove(Socket socket)
    {
        if (socket == null)
            throw new ArgumentNullException(nameof(socket));
        int idx = _items.FindIndex(item => item.Socket == socket);
        if (idx < 0)
            return false;
        _items.RemoveAt(idx);
        return true;
    }

    public bool RemoveFd(int fd)
    {
        int idx = _items.FindIndex(item => item.Socket == null && item.Fd == fd);
        if (idx < 0)
            return false;
        _items.RemoveAt(idx);
        return true;
    }

    public int Wait(List<PollEvent> events, int timeoutMs)
    {
        if (events == null)
            throw new ArgumentNullException(nameof(events));
        events.Clear();
        if (_items.Count == 0)
            return 0;

        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            return WaitWindows(events, timeoutMs);
        return WaitUnix(events, timeoutMs);
    }

    private int WaitUnix(List<PollEvent> events, int timeoutMs)
    {
        var pollItems = new ZlinkPollItemUnix[_items.Count];
        for (int i = 0; i < _items.Count; i++)
        {
            var item = _items[i];
            pollItems[i] = new ZlinkPollItemUnix
            {
                Socket = item.Socket?.Handle ?? IntPtr.Zero,
                Fd = item.Fd,
                Events = (short)item.Events,
                Revents = 0
            };
        }

        int rc = NativeMethods.zlink_poll_unix(pollItems, pollItems.Length,
            timeoutMs);
        if (rc <= 0)
            return rc;

        for (int i = 0; i < pollItems.Length; i++)
        {
            if (pollItems[i].Revents == 0)
                continue;
            events.Add(new PollEvent(_items[i].Socket, _items[i].Tag,
                (PollEvents)pollItems[i].Events,
                (PollEvents)pollItems[i].Revents));
        }
        return events.Count;
    }

    private int WaitWindows(List<PollEvent> events, int timeoutMs)
    {
        var pollItems = new ZlinkPollItemWindows[_items.Count];
        for (int i = 0; i < _items.Count; i++)
        {
            var item = _items[i];
            pollItems[i] = new ZlinkPollItemWindows
            {
                Socket = item.Socket?.Handle ?? IntPtr.Zero,
                Fd = (ulong)item.Fd,
                Events = (short)item.Events,
                Revents = 0
            };
        }

        int rc = NativeMethods.zlink_poll_windows(pollItems, pollItems.Length,
            timeoutMs);
        if (rc <= 0)
            return rc;

        for (int i = 0; i < pollItems.Length; i++)
        {
            if (pollItems[i].Revents == 0)
                continue;
            events.Add(new PollEvent(_items[i].Socket, _items[i].Tag,
                (PollEvents)pollItems[i].Events,
                (PollEvents)pollItems[i].Revents));
        }
        return events.Count;
    }

    private sealed class PollItem
    {
        public PollItem(Socket? socket, int fd, PollEvents events, object? tag)
        {
            Socket = socket;
            Fd = fd;
            Events = events;
            Tag = tag;
        }

        public Socket? Socket { get; }
        public int Fd { get; }
        public PollEvents Events { get; }
        public object? Tag { get; }
    }
}

public readonly struct PollEvent
{
    public PollEvent(Socket? socket, object? tag, PollEvents events,
        PollEvents revents)
    {
        Socket = socket;
        Tag = tag;
        Events = events;
        Revents = revents;
    }

    public Socket? Socket { get; }
    public object? Tag { get; }
    public PollEvents Events { get; }
    public PollEvents Revents { get; }
}
