// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Systems.Zlink.Native;

namespace Systems.Zlink;

public sealed class Poller : IDisposable, IAsyncDisposable
{
    private const int MaxUserDataSlotCount = 65536;

    private static readonly string[] RequiredExports = new[]
    {
        "zlink_poller_new",
        "zlink_poller_destroy",
        "zlink_poller_size",
        "zlink_poller_add",
        "zlink_poller_add_fd",
        "zlink_poller_add_timer",
        "zlink_poller_modify",
        "zlink_poller_modify_fd",
        "zlink_poller_remove",
        "zlink_poller_remove_fd",
        "zlink_poller_remove_timer",
        "zlink_poller_wait"
    };

    private readonly List<PollItem> _items = new();
    private readonly Dictionary<nint, PollItem> _itemsByUserData = new();
    private readonly List<PollItem?> _itemsByUserDataSlot = new();
    private ZlinkPollerEvent[] _nativeEvents = Array.Empty<ZlinkPollerEvent>();
    private IntPtr _handle;
    private nint _nextUserData = 1;

    public Poller()
    {
        List<string> missing = GetMissingExports();
        if (missing.Count > 0)
        {
            throw new ZlinkConfigException(ConfigResult.NotSupported,
                (int)ErrorCode.ENotSup);
        }

        _handle = NativeMethods.zlink_poller_new();
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        _items.Clear();
        _itemsByUserData.Clear();
        _nativeEvents = Array.Empty<ZlinkPollerEvent>();
    }

    internal int Count
    {
        get
        {
            EnsureNotDisposed();
            int rc = NativeMethods.zlink_poller_size(_handle, out _);
            if (rc < 0)
                throw ZlinkException.CreateConfigException(
                    NativeMethods.zlink_errno());
            return rc;
        }
    }

    public int Size => Count;

    public void Add(IZlinkSocket socket, PollEventFlags events, object? tag = null)
    {
        EnsureNotDisposed();
        SocketBase concreteSocket = SocketInterop.RequireSocket(socket,
            nameof(socket));
        EnumValidation.EnsurePollEvents(events, nameof(events));

        IntPtr userData = AllocateUserData();
        int rc = NativeMethods.zlink_poller_add(_handle, concreteSocket.Handle,
            userData, (short)events);
        if (rc != 0)
        {
            ReleaseUserData(userData);
            ZlinkException.ThrowConfigIfError(rc);
        }
        RegisterItem(new PollItem(PollItemKind.Socket, concreteSocket, userData,
            concreteSocket.Handle, 0, null, events, tag));
    }

    public void AddFd(int fd, PollEventFlags events, object? tag = null)
    {
        EnsureNotDisposed();
        EnumValidation.EnsurePollEvents(events, nameof(events));

        IntPtr userData = AllocateUserData();
        int rc = NativeMethods.zlink_poller_add_fd(_handle, fd, userData,
            (short)events);
        if (rc != 0)
        {
            ReleaseUserData(userData);
            ZlinkException.ThrowConfigIfError(rc);
        }
        RegisterItem(new PollItem(PollItemKind.Fd, null, userData, IntPtr.Zero,
            fd, null, events, tag));
    }

    public void Add(Timer timer, object? tag = null)
    {
        EnsureNotDisposed();
        if (timer == null)
            throw new ArgumentNullException(nameof(timer));

        IntPtr userData = AllocateUserData();
        int rc = NativeMethods.zlink_poller_add_timer(_handle, timer.Handle,
            userData);
        if (rc != 0)
        {
            ReleaseUserData(userData);
            ZlinkException.ThrowConfigIfError(rc);
        }
        RegisterItem(new PollItem(PollItemKind.Timer, null, userData,
            IntPtr.Zero, 0, timer, PollEventFlags.PollIn, tag));
    }

    public void Modify(IZlinkSocket socket, PollEventFlags events)
    {
        EnsureNotDisposed();
        SocketBase concreteSocket = SocketInterop.RequireSocket(socket,
            nameof(socket));
        EnumValidation.EnsurePollEvents(events, nameof(events));

        int index = FindSocket(concreteSocket.Handle);
        if (index < 0)
            throw new ArgumentException("socket is not registered",
                nameof(socket));

        int rc = NativeMethods.zlink_poller_modify(_handle, concreteSocket.Handle,
            (short)events);
        ZlinkException.ThrowConfigIfError(rc);
        _items[index].Events = events;
    }

    public void ModifyFd(int fd, PollEventFlags events)
    {
        EnsureNotDisposed();
        EnumValidation.EnsurePollEvents(events, nameof(events));

        int index = FindFd(fd);
        if (index < 0)
            throw new ArgumentException("fd is not registered", nameof(fd));

        int rc = NativeMethods.zlink_poller_modify_fd(_handle, fd,
            (short)events);
        ZlinkException.ThrowConfigIfError(rc);
        _items[index].Events = events;
    }

    public bool Remove(IZlinkSocket socket)
    {
        EnsureNotDisposed();
        SocketBase concreteSocket = SocketInterop.RequireSocket(socket,
            nameof(socket));

        int index = FindSocket(concreteSocket.Handle);
        if (index < 0)
            return false;

        int rc = NativeMethods.zlink_poller_remove(_handle, concreteSocket.Handle);
        ZlinkException.ThrowConfigIfError(rc);
        UnregisterItem(index);
        return true;
    }

    public bool Remove(Timer timer)
    {
        EnsureNotDisposed();
        if (timer == null)
            throw new ArgumentNullException(nameof(timer));

        int index = FindTimer(timer.Handle);
        if (index < 0)
            return false;

        int rc = NativeMethods.zlink_poller_remove_timer(_handle, timer.Handle);
        ZlinkException.ThrowConfigIfError(rc);
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
        ZlinkException.ThrowConfigIfError(rc);
        UnregisterItem(index);
        return true;
    }

    public void Clear()
    {
        EnsureNotDisposed();

        IntPtr handle = _handle;
        int rc = NativeMethods.zlink_poller_destroy(ref handle);
        ZlinkException.ThrowConfigIfError(rc);

        _handle = NativeMethods.zlink_poller_new();
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        _items.Clear();
        _itemsByUserData.Clear();
        _itemsByUserDataSlot.Clear();
        _nativeEvents = Array.Empty<ZlinkPollerEvent>();
        _nextUserData = 1;
    }

    public void Close()
    {
        Dispose();
    }

    internal int Wait(List<PollEvent> events, int timeoutMs)
    {
        EnsureNotDisposed();
        if (events == null)
            throw new ArgumentNullException(nameof(events));

        events.Clear();
        if (_items.Count == 0)
            return 0;

        EnsureEventCapacity(_items.Count);
        int ready = NativeMethods.zlink_poller_wait(_handle, _nativeEvents,
            _items.Count, timeoutMs, out _);
        if (ready < 0)
            throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
        if (ready == 0)
            return 0;

        for (int i = 0; i < ready; i++)
            events.Add(MapEvent(_nativeEvents[i]));
        return ready;
    }

    public PollEvent? Wait(TimeSpan timeout)
    {
        PollEvent[] destination = new PollEvent[1];
        int written = WaitCore(destination, ToTimeoutMilliseconds(timeout),
            out _);
        return written == 0 ? null : destination[0];
    }

    public int Wait(Span<PollEvent> destination, TimeSpan timeout,
        out int totalReady)
    {
        return WaitCore(destination, ToTimeoutMilliseconds(timeout), out totalReady);
    }

    internal int WaitReadyTags(Span<int> tags, Span<PollEventFlags> revents,
        TimeSpan timeout, out int totalReady)
    {
        return WaitReadyTagsCore(tags, revents, ToTimeoutMilliseconds(timeout),
            out totalReady);
    }

    private int WaitCore(Span<PollEvent> destination, int timeoutMs,
        out int totalReady)
    {
        EnsureNotDisposed();
        if (_items.Count == 0)
        {
            totalReady = 0;
            return 0;
        }

        EnsureEventCapacity(_items.Count);
        int ready = NativeMethods.zlink_poller_wait(_handle, _nativeEvents,
            _items.Count, timeoutMs, out _);
        if (ready < 0)
            throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
        totalReady = ready;
        if (ready == 0)
            return 0;

        int written = Math.Min(destination.Length, ready);
        for (int i = 0; i < written; i++)
            destination[i] = MapEvent(_nativeEvents[i]);
        return written;
    }

    private int WaitReadyTagsCore(Span<int> tags, Span<PollEventFlags> revents,
        int timeoutMs, out int totalReady)
    {
        EnsureNotDisposed();
        if (_items.Count == 0)
        {
            totalReady = 0;
            return 0;
        }

        EnsureEventCapacity(_items.Count);
        int ready = NativeMethods.zlink_poller_wait(_handle, _nativeEvents,
            _items.Count, timeoutMs, out _);
        if (ready < 0)
            throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
        totalReady = ready;
        if (ready == 0)
            return 0;

        int capacity = Math.Min(tags.Length, revents.Length);
        int written = 0;
        for (int i = 0; i < ready && written < capacity; i++)
        {
            ZlinkPollerEvent nativeEvent = _nativeEvents[i];
            PollItem? item = FindEventItem(nativeEvent);
            if (item == null || !item.HasIntTag)
                continue;

            tags[written] = item.IntTag;
            revents[written] = (PollEventFlags)nativeEvent.Events;
            written++;
        }

        return written;
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;

        IntPtr handle = _handle;
        int rc;
        while (true)
        {
            rc = NativeMethods.zlink_poller_destroy(ref handle);
            if (rc == 0)
                break;

            int errno = NativeMethods.zlink_errno();
            ErrorCode code = ZlinkException.MapErrorCode(errno);
            if (code == ErrorCode.EIntr || errno == 4)
                continue;
            break;
        }
        _handle = IntPtr.Zero;
        _items.Clear();
        _itemsByUserData.Clear();
        _itemsByUserDataSlot.Clear();
        _nativeEvents = Array.Empty<ZlinkPollerEvent>();
        if (rc != 0)
            throw ZlinkException.CreateCloseException(NativeMethods.zlink_errno());
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
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

    private static int ToTimeoutMilliseconds(TimeSpan timeout)
    {
        if (timeout < TimeSpan.Zero)
            return -1;
        double millis = timeout.TotalMilliseconds;
        if (millis > int.MaxValue)
            return int.MaxValue;
        return (int)Math.Ceiling(millis);
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
            if (item.Kind == PollItemKind.Fd && item.Fd == fd)
                return i;
        }
        return -1;
    }

    private int FindTimer(IntPtr handle)
    {
        for (int i = 0; i < _items.Count; i++)
        {
            PollItem item = _items[i];
            if (item.Kind == PollItemKind.Timer && item.Timer?.Handle == handle)
                return i;
        }
        return -1;
    }

    private PollEvent MapEvent(ZlinkPollerEvent nativeEvent)
    {
        PollItem? item = FindEventItem(nativeEvent);
        int? fd = item?.Kind == PollItemKind.Fd ? item.Fd : null;
        Timer? timer = item?.Kind == PollItemKind.Timer ? item.Timer : null;
        if (!fd.HasValue && timer == null && nativeEvent.Socket == IntPtr.Zero
            && nativeEvent.Timer == IntPtr.Zero)
            fd = nativeEvent.Fd;
        return new PollEvent(item?.Socket, fd, timer, item?.Tag,
            item?.Events ?? (PollEventFlags)nativeEvent.Events,
            (PollEventFlags)nativeEvent.Events);
    }

    private PollItem? FindEventItem(ZlinkPollerEvent nativeEvent)
    {
        PollItem? item = FindUserDataItem(nativeEvent.UserData);
        item ??= nativeEvent.Socket != IntPtr.Zero
            ? FindSocketItem(nativeEvent.Socket)
            : nativeEvent.Timer != IntPtr.Zero
                ? FindTimerItem(nativeEvent.Timer)
                : FindFdItem(nativeEvent.Fd);
        return item;
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

    private PollItem? FindTimerItem(IntPtr handle)
    {
        int index = FindTimer(handle);
        return index >= 0 ? _items[index] : null;
    }

    private PollItem? FindUserDataItem(IntPtr userData)
    {
        nint value = userData;
        if (value > 0 && value <= _itemsByUserDataSlot.Count)
        {
            PollItem? slotItem = _itemsByUserDataSlot[(int)value - 1];
            if (slotItem?.UserData == userData)
                return slotItem;
        }

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
        if (userData == IntPtr.Zero)
            return;

        nint value = userData;
        if (value > 0 && value <= _itemsByUserDataSlot.Count)
            _itemsByUserDataSlot[(int)value - 1] = null;
        _itemsByUserData.Remove(userData);
    }

    private void RegisterItem(PollItem item)
    {
        _items.Add(item);
        if (item.UserData != IntPtr.Zero)
        {
            _itemsByUserData[item.UserData] = item;
            nint value = item.UserData;
            if (value > 0 && value <= MaxUserDataSlotCount)
            {
                int index = (int)value - 1;
                while (_itemsByUserDataSlot.Count <= index)
                    _itemsByUserDataSlot.Add(null);
                _itemsByUserDataSlot[index] = item;
            }
        }
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
        Fd,
        Timer
    }

    private sealed class PollItem
    {
        public PollItem(PollItemKind kind, IZlinkSocket? socket, IntPtr userData,
            IntPtr socketHandle, int fd, Timer? timer, PollEventFlags events,
            object? tag)
        {
            Kind = kind;
            Socket = socket;
            UserData = userData;
            SocketHandle = socketHandle;
            Fd = fd;
            Timer = timer;
            Events = events;
            Tag = tag;
            if (tag is int intTag)
            {
                HasIntTag = true;
                IntTag = intTag;
            }
        }

        public PollItemKind Kind { get; }
        public IZlinkSocket? Socket { get; }
        public IntPtr UserData { get; }
        public IntPtr SocketHandle { get; }
        public int Fd { get; }
        public Timer? Timer { get; }
        public PollEventFlags Events { get; set; }
        public object? Tag { get; }
        public int IntTag { get; }
        public bool HasIntTag { get; }
        public bool IsSocket => Kind == PollItemKind.Socket;
    }
}

public readonly struct PollEvent
{
    internal PollEvent(IZlinkSocket? socket, int? fd, Timer? timer, object? tag,
        PollEventFlags events, PollEventFlags revents)
    {
        Socket = socket;
        Fd = fd;
        Timer = timer;
        Tag = tag;
        Events = events;
        Revents = revents;
    }

    public IZlinkSocket? Socket { get; }
    public int? Fd { get; }
    public Timer? Timer { get; }
    public object? Tag { get; }
    public PollEventFlags Events { get; }
    public PollEventFlags Revents { get; }
}
