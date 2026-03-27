// SPDX-License-Identifier: MPL-2.0

using System;
using Zlink.Service;
using Zlink.Sockets.Internal;

namespace Zlink;

public abstract class SocketBase : IDisposable
{
    private readonly SocketKernel _kernel;

    internal SocketBase(Context context, SocketType type)
    {
        _kernel = new SocketKernel(context, type);
    }

    internal SocketBase(SocketKernel kernel)
    {
        _kernel = kernel ?? throw new ArgumentNullException(nameof(kernel));
    }

    internal IntPtr Handle => _kernel.Handle;
    internal SocketKernel Kernel => _kernel;

    public void Bind(string address)
    {
        _kernel.Bind(address);
    }

    public void Connect(string address)
    {
        _kernel.Connect(address);
    }

    public void Unbind(string address)
    {
        _kernel.Unbind(address);
    }

    public void Disconnect(string address)
    {
        _kernel.Disconnect(address);
    }

    public void AttachDiscovery(Discovery discovery)
    {
        _kernel.AttachDiscovery(discovery);
    }

    public void SendReadyHandler(Action handler)
    {
        _kernel.SendReadyHandler(handler);
    }

    public SocketMonitor OpenMonitor(SocketEvent events)
    {
        return _kernel.OpenMonitor(events);
    }

    public void SetOption(SocketOptionKey<int> option, int value)
    {
        _kernel.SetOption(option, value);
    }

    public void SetOption(SocketOptionKey<long> option, long value)
    {
        _kernel.SetOption(option, value);
    }

    public void SetOption(SocketOptionKey<ulong> option, ulong value)
    {
        _kernel.SetOption(option, value);
    }

    public void SetOption(SocketOptionKey<byte[]> option, byte[] value)
    {
        _kernel.SetOption(option, value);
    }

    public void SetOption(SocketOptionKey<byte[]> option, ReadOnlySpan<byte> value)
    {
        _kernel.SetOption(option, value);
    }

    public void SetOption(SocketOptionKey<string> option, string value)
    {
        _kernel.SetOption(option, value);
    }

    public int GetOption(SocketOptionKey<int> option)
    {
        return _kernel.GetOption(option);
    }

    public long GetOption(SocketOptionKey<long> option)
    {
        return _kernel.GetOption(option);
    }

    public ulong GetOption(SocketOptionKey<ulong> option)
    {
        return _kernel.GetOption(option);
    }

    public byte[] GetOption(SocketOptionKey<byte[]> option, int initialSize = 256)
    {
        return _kernel.GetOption(option, initialSize);
    }

    public int GetOption(SocketOptionKey<byte[]> option, Span<byte> destination)
    {
        return _kernel.GetOption(option, destination);
    }

    public string GetOption(SocketOptionKey<string> option, int initialSize = 256)
    {
        return _kernel.GetOption(option, initialSize);
    }

    public void Dispose()
    {
        _kernel.Dispose();
        GC.SuppressFinalize(this);
    }
}
