// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using Zlink.Native;

namespace Zlink;

public sealed class Poller : IDisposable
{
    private static readonly string[] RequiredExports = new[]
    {
        "zlink_poller_new",
        "zlink_poller_destroy",
        "zlink_poller_size",
        "zlink_poller_add",
        "zlink_poller_add_fd",
        "zlink_poller_modify",
        "zlink_poller_modify_fd",
        "zlink_poller_remove",
        "zlink_poller_remove_fd",
        "zlink_poller_wait_all"
    };

    private readonly List<PollItem> _items = new();
    private ZlinkPollerEvent[] _nativeEvents = Array.Empty<ZlinkPollerEvent>();
    private IntPtr _handle;

    public Poller()
    {
        List<string> missing = GetMissingExports();
        if (missing.Count > 0)
        {
            throw new NotSupportedException(
                "Poller API is not available in the loaded native zlink library. "
                + "Missing exports: " + string.Join(", ", missing));
        }

        _handle = NativeMethods.zlink_poller_new();
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    public int Count
    {
        get
        {
            EnsureNotDisposed();
            int rc = NativeMethods.zlink_poller_size(_handle);
            ZlinkException.ThrowIfError(rc);
            return rc;
        }
    }

    public void Add(Socket socket, PollEvents events, object? tag = null)
    {
        EnsureNotDisposed();
        if (socket == null)
            throw new ArgumentNullException(nameof(socket));

        int rc = NativeMethods.zlink_poller_add(_handle, socket.Handle,
            IntPtr.Zero, (short)events);
        ZlinkException.ThrowIfError(rc);
        _items.Add(new PollItem(socket, socket.Handle, 0, events, tag, true));
    }

    public void AddFd(int fd, PollEvents events, object? tag = null)
    {
        EnsureNotDisposed();

        int rc = NativeMethods.zlink_poller_add_fd(_handle, fd, IntPtr.Zero,
            (short)events);
        ZlinkException.ThrowIfError(rc);
        _items.Add(new PollItem(null, IntPtr.Zero, fd, events, tag, false));
    }

    public void Modify(Socket socket, PollEvents events)
    {
        EnsureNotDisposed();
        if (socket == null)
            throw new ArgumentNullException(nameof(socket));

        int index = FindSocket(socket.Handle);
        if (index < 0)
            throw new ArgumentException("socket is not registered",
                nameof(socket));

        int rc = NativeMethods.zlink_poller_modify(_handle, socket.Handle,
            (short)events);
        ZlinkException.ThrowIfError(rc);
        _items[index].Events = events;
    }

    public void ModifyFd(int fd, PollEvents events)
    {
        EnsureNotDisposed();

        int index = FindFd(fd);
        if (index < 0)
            throw new ArgumentException("fd is not registered", nameof(fd));

        int rc = NativeMethods.zlink_poller_modify_fd(_handle, fd,
            (short)events);
        ZlinkException.ThrowIfError(rc);
        _items[index].Events = events;
    }

    public bool Remove(Socket socket)
    {
        EnsureNotDisposed();
        if (socket == null)
            throw new ArgumentNullException(nameof(socket));

        int index = FindSocket(socket.Handle);
        if (index < 0)
            return false;

        int rc = NativeMethods.zlink_poller_remove(_handle, socket.Handle);
        ZlinkException.ThrowIfError(rc);
        _items.RemoveAt(index);
        return true;
    }

    public bool Remove(int fd)
    {
        EnsureNotDisposed();

        int index = FindFd(fd);
        if (index < 0)
            return false;

        int rc = NativeMethods.zlink_poller_remove_fd(_handle, fd);
        ZlinkException.ThrowIfError(rc);
        _items.RemoveAt(index);
        return true;
    }

    public void Clear()
    {
        EnsureNotDisposed();

        IntPtr handle = _handle;
        int rc = NativeMethods.zlink_poller_destroy(ref handle);
        ZlinkException.ThrowIfError(rc);

        _handle = NativeMethods.zlink_poller_new();
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();

        _items.Clear();
        _nativeEvents = Array.Empty<ZlinkPollerEvent>();
    }

    public int Wait(List<PollEvent> events, int timeoutMs)
    {
        EnsureNotDisposed();
        if (events == null)
            throw new ArgumentNullException(nameof(events));

        events.Clear();
        if (_items.Count == 0)
            return 0;

        EnsureEventCapacity(_items.Count);
        int ready = NativeMethods.zlink_poller_wait_all(_handle, _nativeEvents,
            _items.Count, timeoutMs);
        ZlinkException.ThrowIfError(ready);
        if (ready == 0)
            return 0;

        for (int i = 0; i < ready; i++)
            events.Add(MapEvent(_nativeEvents[i]));
        return ready;
    }

    public int Wait(Span<PollEvent> destination, int timeoutMs,
        out int totalReady)
    {
        EnsureNotDisposed();
        if (_items.Count == 0)
        {
            totalReady = 0;
            return 0;
        }

        EnsureEventCapacity(_items.Count);
        int ready = NativeMethods.zlink_poller_wait_all(_handle, _nativeEvents,
            _items.Count, timeoutMs);
        ZlinkException.ThrowIfError(ready);
        totalReady = ready;
        if (ready == 0)
            return 0;

        int written = Math.Min(destination.Length, ready);
        for (int i = 0; i < written; i++)
            destination[i] = MapEvent(_nativeEvents[i]);
        return written;
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;

        IntPtr handle = _handle;
        NativeMethods.zlink_poller_destroy(ref handle);
        _handle = IntPtr.Zero;
        _items.Clear();
        _nativeEvents = Array.Empty<ZlinkPollerEvent>();
        GC.SuppressFinalize(this);
    }

    ~Poller()
    {
        Dispose();
    }

    private static List<string> GetMissingExports()
    {
        var missing = new List<string>();
        foreach (string symbol in RequiredExports)
        {
            if (!NativeLibraryLoader.HasExport(symbol))
                missing.Add(symbol);
        }
        return missing;
    }

    private void EnsureEventCapacity(int count)
    {
        if (_nativeEvents.Length < count)
            _nativeEvents = new ZlinkPollerEvent[count];
    }

    private int FindSocket(IntPtr handle)
    {
        for (int i = 0; i < _items.Count; i++)
        {
            PollItem item = _items[i];
            if (item.IsSocket && item.SocketHandle == handle)
                return i;
        }
        return -1;
    }

    private int FindFd(int fd)
    {
        for (int i = 0; i < _items.Count; i++)
        {
            PollItem item = _items[i];
            if (!item.IsSocket && item.Fd == fd)
                return i;
        }
        return -1;
    }

    private PollEvent MapEvent(ZlinkPollerEvent nativeEvent)
    {
        PollItem? item = nativeEvent.Socket != IntPtr.Zero
            ? FindSocketItem(nativeEvent.Socket)
            : FindFdItem(nativeEvent.Fd);
        return new PollEvent(item?.Socket, item?.Tag,
            item?.Events ?? (PollEvents)nativeEvent.Events,
            (PollEvents)nativeEvent.Events);
    }

    private PollItem? FindSocketItem(IntPtr handle)
    {
        int index = FindSocket(handle);
        return index >= 0 ? _items[index] : null;
    }

    private PollItem? FindFdItem(int fd)
    {
        int index = FindFd(fd);
        return index >= 0 ? _items[index] : null;
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Poller));
    }

    private sealed class PollItem
    {
        public PollItem(Socket? socket, IntPtr socketHandle, int fd,
            PollEvents events, object? tag, bool isSocket)
        {
            Socket = socket;
            SocketHandle = socketHandle;
            Fd = fd;
            Events = events;
            Tag = tag;
            IsSocket = isSocket;
        }

        public Socket? Socket { get; }
        public IntPtr SocketHandle { get; }
        public int Fd { get; }
        public PollEvents Events { get; set; }
        public object? Tag { get; }
        public bool IsSocket { get; }
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
