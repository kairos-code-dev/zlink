// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Systems.Zlink.Native;

namespace Systems.Zlink;

internal sealed class Poller : IPoller
{
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
    private ZlinkPollerEvent[] _nativeEvents = Array.Empty<ZlinkPollerEvent>();
    private IntPtr _handle;

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

    public void Add(IZlinkSocket socket, PollEventFlags events, nuint slot)
    {
        EnsureNotDisposed();
        IntPtr socketHandle = SocketInterop.RequirePollableHandle(socket,
            nameof(socket));
        EnumValidation.EnsurePollEvents(events, nameof(events));

        IntPtr userData = SlotToUserData(slot);
        int rc = NativeMethods.zlink_poller_add(_handle, socketHandle,
            userData, (short)events);
        if (rc != 0)
            ZlinkException.ThrowConfigIfError(rc);
        RegisterItem(new PollItem(PollItemKind.Socket, socket, socketHandle, 0,
            null, events, slot));
    }

    public void AddFd(int fd, PollEventFlags events, nuint slot)
    {
        EnsureNotDisposed();
        EnumValidation.EnsurePollEvents(events, nameof(events));

        IntPtr userData = SlotToUserData(slot);
        int rc = NativeMethods.zlink_poller_add_fd(_handle, fd, userData,
            (short)events);
        if (rc != 0)
            ZlinkException.ThrowConfigIfError(rc);
        RegisterItem(new PollItem(PollItemKind.Fd, null, IntPtr.Zero, fd, null,
            events, slot));
    }

    public void Add(IZlinkTimer timer, nuint slot)
    {
        EnsureNotDisposed();
        Timer concreteTimer = SocketInterop.RequireTimer(timer, nameof(timer));

        IntPtr userData = SlotToUserData(slot);
        int rc = NativeMethods.zlink_poller_add_timer(_handle,
            concreteTimer.Handle, userData);
        if (rc != 0)
            ZlinkException.ThrowConfigIfError(rc);
        RegisterItem(new PollItem(PollItemKind.Timer, null, IntPtr.Zero, 0,
            concreteTimer, PollEventFlags.PollIn, slot));
    }

    public void Modify(IZlinkSocket socket, PollEventFlags events)
    {
        EnsureNotDisposed();
        IntPtr socketHandle = SocketInterop.RequirePollableHandle(socket,
            nameof(socket));
        EnumValidation.EnsurePollEvents(events, nameof(events));

        int index = FindSocket(socketHandle);
        if (index < 0)
            throw new ArgumentException("socket is not registered",
                nameof(socket));

        int rc = NativeMethods.zlink_poller_modify(_handle, socketHandle,
            (short)events);
        ZlinkException.ThrowConfigIfError(rc);
        UnregisterExternalProgress(_items[index]);
        _items[index].Events = events;
        RegisterExternalProgress(_items[index]);
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
        IntPtr socketHandle = SocketInterop.RequirePollableHandle(socket,
            nameof(socket));

        int index = FindSocket(socketHandle);
        if (index < 0)
            return false;

        int rc = NativeMethods.zlink_poller_remove(_handle, socketHandle);
        ZlinkException.ThrowConfigIfError(rc);
        UnregisterItem(index);
        return true;
    }

    public bool Remove(IZlinkTimer timer)
    {
        EnsureNotDisposed();
        Timer concreteTimer = SocketInterop.RequireTimer(timer, nameof(timer));

        int index = FindTimer(concreteTimer.Handle);
        if (index < 0)
            return false;

        int rc = NativeMethods.zlink_poller_remove_timer(_handle,
            concreteTimer.Handle);
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
        UnregisterAllExternalProgress();
        _items.Clear();
        _nativeEvents = Array.Empty<ZlinkPollerEvent>();
    }

    public void Close()
    {
        Dispose();
    }

    public int Wait(Span<PollEvent> destination, TimeSpan timeout)
    {
        EnsureNotDisposed();
        if (destination.Length == 0)
            throw new ArgumentException("destination must not be empty.",
                nameof(destination));
        if (_items.Count == 0)
            return 0;

        int capacity = Math.Min(destination.Length, _items.Count);
        EnsureEventCapacity(capacity);
        int ready = WaitNative(ToTimeoutMilliseconds(timeout), capacity);
        if (ready < 0)
            throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
        if (ready == 0)
            return 0;

        for (int i = 0; i < ready; i++)
            destination[i] = MapEvent(_nativeEvents[i]);
        return ready;
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
        UnregisterAllExternalProgress();
        _items.Clear();
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

    private unsafe int WaitNative(int timeoutMs, int capacity)
    {
        fixed (ZlinkPollerEvent* events = _nativeEvents)
        {
            return NativeMethods.zlink_poller_wait_pinned(_handle, events,
                capacity, timeoutMs, out _);
        }
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
        PollSourceKind sourceKind = (PollSourceKind)nativeEvent.MonitorSourceKind;
        int fd = sourceKind == PollSourceKind.Fd ? nativeEvent.Fd : 0;
        return new PollEvent(sourceKind, UserDataToSlot(nativeEvent.UserData),
            (PollEventFlags)nativeEvent.Events, fd);
    }

    private static IntPtr SlotToUserData(nuint slot)
    {
        return unchecked((IntPtr)slot);
    }

    private static nuint UserDataToSlot(IntPtr userData)
    {
        return unchecked((nuint)userData);
    }

    private void RegisterItem(PollItem item)
    {
        RegisterExternalProgress(item);
        _items.Add(item);
    }

    private void UnregisterItem(int index)
    {
        PollItem item = _items[index];
        UnregisterExternalProgress(item);
        _items.RemoveAt(index);
    }

    private static void RegisterExternalProgress(PollItem item)
    {
        if (item.IsSocket
            && (item.Events & PollEventFlags.PollCompletion) != 0)
            RequestProgressPump.AcquireExternalProgress(item.SocketHandle);
    }

    private static void UnregisterExternalProgress(PollItem item)
    {
        if (item.IsSocket
            && (item.Events & PollEventFlags.PollCompletion) != 0)
            RequestProgressPump.ReleaseExternalProgress(item.SocketHandle);
    }

    private void UnregisterAllExternalProgress()
    {
        foreach (PollItem item in _items)
            UnregisterExternalProgress(item);
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
        public PollItem(PollItemKind kind, IZlinkSocket? socket,
            IntPtr socketHandle, int fd, Timer? timer, PollEventFlags events,
            nuint slot)
        {
            Kind = kind;
            Socket = socket;
            SocketHandle = socketHandle;
            Fd = fd;
            Timer = timer;
            Events = events;
            Slot = slot;
        }

        public PollItemKind Kind { get; }
        public IZlinkSocket? Socket { get; }
        public IntPtr SocketHandle { get; }
        public int Fd { get; }
        public Timer? Timer { get; }
        public PollEventFlags Events { get; set; }
        public nuint Slot { get; }
        public bool IsSocket => Kind == PollItemKind.Socket;
    }
}
