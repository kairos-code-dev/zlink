// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using Zlink.Service;
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
        "zlink_poller_add_spot_sub",
        "zlink_poller_add_spot_pub",
        "zlink_poller_add_receiver",
        "zlink_poller_add_fd",
        "zlink_poller_modify",
        "zlink_poller_modify_spot_sub",
        "zlink_poller_modify_spot_pub",
        "zlink_poller_modify_receiver",
        "zlink_poller_modify_fd",
        "zlink_poller_remove",
        "zlink_poller_remove_spot_sub",
        "zlink_poller_remove_spot_pub",
        "zlink_poller_remove_receiver",
        "zlink_poller_remove_fd",
        "zlink_poller_wait_all"
    };

    private readonly List<PollItem> _items = new();
    private readonly Dictionary<nint, PollItem> _itemsByUserData = new();
    private ZlinkPollerEvent[] _nativeEvents = Array.Empty<ZlinkPollerEvent>();
    private IntPtr _handle;
    private nint _nextUserData = 1;

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

        IntPtr userData = AllocateUserData();
        int rc = NativeMethods.zlink_poller_add(_handle, socket.Handle,
            userData, (short)events);
        if (rc != 0)
        {
            ReleaseUserData(userData);
            ZlinkException.ThrowIfError(rc);
        }
        RegisterItem(new PollItem(PollItemKind.Socket, socket, null, userData,
            socket.Handle, 0, events, tag));
    }

    public void AddSpotSub(Spot spot, PollEvents events, object? tag = null)
    {
        EnsureNotDisposed();
        if (spot == null)
            throw new ArgumentNullException(nameof(spot));

        IntPtr userData = AllocateUserData();
        int rc = NativeMethods.zlink_poller_add_spot_sub(_handle, spot.SubHandle,
            userData, (short)events);
        if (rc != 0)
        {
            ReleaseUserData(userData);
            ZlinkException.ThrowIfError(rc);
        }
        RegisterItem(new PollItem(PollItemKind.SpotSub, null, spot, userData,
            IntPtr.Zero, 0, events, tag));
    }

    public void AddSpotPub(Spot spot, PollEvents events, object? tag = null)
    {
        EnsureNotDisposed();
        if (spot == null)
            throw new ArgumentNullException(nameof(spot));

        IntPtr userData = AllocateUserData();
        int rc = NativeMethods.zlink_poller_add_spot_pub(_handle, spot.PubHandle,
            userData, (short)events);
        if (rc != 0)
        {
            ReleaseUserData(userData);
            ZlinkException.ThrowIfError(rc);
        }
        RegisterItem(new PollItem(PollItemKind.SpotPub, null, spot, userData,
            IntPtr.Zero, 0, events, tag));
    }

    public void AddReceiver(Receiver receiver, PollEvents events,
        object? tag = null)
    {
        EnsureNotDisposed();
        if (receiver == null)
            throw new ArgumentNullException(nameof(receiver));

        IntPtr userData = AllocateUserData();
        int rc = NativeMethods.zlink_poller_add_receiver(_handle, receiver.Handle,
            userData, (short)events);
        if (rc != 0)
        {
            ReleaseUserData(userData);
            ZlinkException.ThrowIfError(rc);
        }
        RegisterItem(new PollItem(PollItemKind.Receiver, null, receiver,
            userData, IntPtr.Zero, 0, events, tag));
    }

    public void AddFd(int fd, PollEvents events, object? tag = null)
    {
        EnsureNotDisposed();

        IntPtr userData = AllocateUserData();
        int rc = NativeMethods.zlink_poller_add_fd(_handle, fd, userData,
            (short)events);
        if (rc != 0)
        {
            ReleaseUserData(userData);
            ZlinkException.ThrowIfError(rc);
        }
        RegisterItem(new PollItem(PollItemKind.Fd, null, null, userData,
            IntPtr.Zero, fd, events, tag));
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

    public void ModifySpotSub(Spot spot, PollEvents events)
    {
        EnsureNotDisposed();
        if (spot == null)
            throw new ArgumentNullException(nameof(spot));

        int index = FindService(PollItemKind.SpotSub, spot);
        if (index < 0)
            throw new ArgumentException("spot sub is not registered",
                nameof(spot));

        int rc = NativeMethods.zlink_poller_modify_spot_sub(_handle,
            spot.SubHandle, (short)events);
        ZlinkException.ThrowIfError(rc);
        _items[index].Events = events;
    }

    public void ModifySpotPub(Spot spot, PollEvents events)
    {
        EnsureNotDisposed();
        if (spot == null)
            throw new ArgumentNullException(nameof(spot));

        int index = FindService(PollItemKind.SpotPub, spot);
        if (index < 0)
            throw new ArgumentException("spot pub is not registered",
                nameof(spot));

        int rc = NativeMethods.zlink_poller_modify_spot_pub(_handle,
            spot.PubHandle, (short)events);
        ZlinkException.ThrowIfError(rc);
        _items[index].Events = events;
    }

    public void ModifyReceiver(Receiver receiver, PollEvents events)
    {
        EnsureNotDisposed();
        if (receiver == null)
            throw new ArgumentNullException(nameof(receiver));

        int index = FindService(PollItemKind.Receiver, receiver);
        if (index < 0)
            throw new ArgumentException("receiver is not registered",
                nameof(receiver));

        int rc = NativeMethods.zlink_poller_modify_receiver(_handle,
            receiver.Handle, (short)events);
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
        UnregisterItem(index);
        return true;
    }

    public bool RemoveSpotSub(Spot spot)
    {
        EnsureNotDisposed();
        if (spot == null)
            throw new ArgumentNullException(nameof(spot));

        int index = FindService(PollItemKind.SpotSub, spot);
        if (index < 0)
            return false;

        int rc = NativeMethods.zlink_poller_remove_spot_sub(_handle,
            spot.SubHandle);
        ZlinkException.ThrowIfError(rc);
        UnregisterItem(index);
        return true;
    }

    public bool RemoveSpotPub(Spot spot)
    {
        EnsureNotDisposed();
        if (spot == null)
            throw new ArgumentNullException(nameof(spot));

        int index = FindService(PollItemKind.SpotPub, spot);
        if (index < 0)
            return false;

        int rc = NativeMethods.zlink_poller_remove_spot_pub(_handle,
            spot.PubHandle);
        ZlinkException.ThrowIfError(rc);
        UnregisterItem(index);
        return true;
    }

    public bool RemoveReceiver(Receiver receiver)
    {
        EnsureNotDisposed();
        if (receiver == null)
            throw new ArgumentNullException(nameof(receiver));

        int index = FindService(PollItemKind.Receiver, receiver);
        if (index < 0)
            return false;

        int rc = NativeMethods.zlink_poller_remove_receiver(_handle,
            receiver.Handle);
        ZlinkException.ThrowIfError(rc);
        UnregisterItem(index);
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
        UnregisterItem(index);
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
        _itemsByUserData.Clear();
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
        _itemsByUserData.Clear();
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

    private int FindService(PollItemKind kind, object service)
    {
        for (int i = 0; i < _items.Count; i++)
        {
            PollItem item = _items[i];
            if (item.Kind == kind && ReferenceEquals(item.Target, service))
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
        PollItem? item = FindUserDataItem(nativeEvent.UserData);
        item ??= nativeEvent.Socket != IntPtr.Zero
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

    private PollItem? FindUserDataItem(IntPtr userData)
    {
        return _itemsByUserData.TryGetValue(userData, out PollItem? item)
            ? item
            : null;
    }

    private IntPtr AllocateUserData()
    {
        IntPtr userData = (IntPtr)_nextUserData++;
        if (userData == IntPtr.Zero)
            userData = (IntPtr)_nextUserData++;
        return userData;
    }

    private void ReleaseUserData(IntPtr userData)
    {
        if (userData != IntPtr.Zero)
            _itemsByUserData.Remove(userData);
    }

    private void RegisterItem(PollItem item)
    {
        _items.Add(item);
        if (item.UserData != IntPtr.Zero)
            _itemsByUserData[item.UserData] = item;
    }

    private void UnregisterItem(int index)
    {
        PollItem item = _items[index];
        _items.RemoveAt(index);
        ReleaseUserData(item.UserData);
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Poller));
    }

    private enum PollItemKind
    {
        Socket,
        SpotSub,
        SpotPub,
        Receiver,
        Fd
    }

    private sealed class PollItem
    {
        public PollItem(PollItemKind kind, Socket? socket, object? target,
            IntPtr userData, IntPtr socketHandle, int fd, PollEvents events,
            object? tag)
        {
            Kind = kind;
            Socket = socket;
            Target = target;
            UserData = userData;
            SocketHandle = socketHandle;
            Fd = fd;
            Events = events;
            Tag = tag;
        }

        public PollItemKind Kind { get; }
        public Socket? Socket { get; }
        public object? Target { get; }
        public IntPtr UserData { get; }
        public IntPtr SocketHandle { get; }
        public int Fd { get; }
        public PollEvents Events { get; set; }
        public object? Tag { get; }
        public bool IsSocket => Kind == PollItemKind.Socket;
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
