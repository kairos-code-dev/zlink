// SPDX-License-Identifier: MPL-2.0

using System;
using System.ComponentModel;
using Zlink.Service;
using Zlink.Sockets.Internal;

namespace Zlink;

[EditorBrowsable(EditorBrowsableState.Never)]
public abstract class SocketBase : IDisposable, IZlinkSocket
{
    private readonly SocketKernel _kernel;

    internal SocketBase(Context context, SocketType type)
    {
        _kernel = new SocketKernel(context, type);
        Options = new CommonSocketOptions(this);
    }

    internal SocketBase(SocketKernel kernel)
    {
        _kernel = kernel ?? throw new ArgumentNullException(nameof(kernel));
        Options = new CommonSocketOptions(this);
    }

    internal IntPtr Handle => _kernel.Handle;
    internal SocketKernel Kernel => _kernel;
    public CommonSocketOptions Options { get; }

    public void Bind(string address)
    {
        _kernel.Bind(address);
    }

    public void Unbind(string address)
    {
        _kernel.Unbind(address);
    }

    public void AttachDiscovery(Discovery discovery)
    {
        _kernel.AttachDiscovery(discovery);
    }

    public SocketMonitor MonitorOpen(SocketEvent events)
    {
        EnumValidation.EnsureSocketEvents(events, nameof(events));
        return _kernel.MonitorOpen(events);
    }

    public void Close()
    {
        Dispose();
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal void SetOption(SocketOptionKey<int> option, int value)
    {
        _kernel.SetOption(option, value);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal void SetOption(SocketOptionKey<long> option, long value)
    {
        _kernel.SetOption(option, value);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal void SetOption(SocketOptionKey<ulong> option, ulong value)
    {
        _kernel.SetOption(option, value);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal void SetOption(SocketOptionKey<byte[]> option, byte[] value)
    {
        _kernel.SetOption(option, value);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal void SetOption(SocketOptionKey<byte[]> option, ReadOnlySpan<byte> value)
    {
        _kernel.SetOption(option, value);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal void SetOption(SocketOptionKey<string> option, string value)
    {
        _kernel.SetOption(option, value);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal int GetOption(SocketOptionKey<int> option)
    {
        if (PerfRawSocketCompat.TryGetInt32Option(this, option,
                out int compatValue))
        {
            return compatValue;
        }
        return _kernel.GetOption(option);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal long GetOption(SocketOptionKey<long> option)
    {
        return _kernel.GetOption(option);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal ulong GetOption(SocketOptionKey<ulong> option)
    {
        return _kernel.GetOption(option);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal byte[] GetOption(SocketOptionKey<byte[]> option, int initialSize = 256)
    {
        return _kernel.GetOption(option, initialSize);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal int GetOption(SocketOptionKey<byte[]> option, Span<byte> destination)
    {
        return _kernel.GetOption(option, destination);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal string GetOption(SocketOptionKey<string> option, int initialSize = 256)
    {
        return _kernel.GetOption(option, initialSize);
    }

    public void Dispose()
    {
        _kernel.Dispose();
        GC.SuppressFinalize(this);
    }
}
