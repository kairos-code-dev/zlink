// SPDX-License-Identifier: MPL-2.0

using System.ComponentModel;
using System.Text;
using Systems.Zlink.Runtime.Native;
using Systems.Zlink.Runtime.Sockets.Internal;

namespace Systems.Zlink;

[EditorBrowsable(EditorBrowsableState.Never)]
internal abstract class SocketBase : ISocket, ISocketOptionEndpoint
{
    internal SocketBase(Context context, SocketType type)
    {
        Kernel = new SocketKernel(context, type);
        Options = new CommonSocketOptions(this);
    }

    internal SocketBase(SocketKernel kernel)
    {
        Kernel = kernel ?? throw new ArgumentNullException(nameof(kernel));
        Options = new CommonSocketOptions(this);
    }

    internal IntPtr Handle => Kernel.Handle;
    internal SocketKernel Kernel { get; }

    internal object SubmitGate { get; } = new();
    public CommonSocketOptions Options { get; }

    public void Bind(string address)
    {
        try
        {
            Kernel.Bind(address);
        }
        catch (ZlinkException ex)
        {
            throw ZlinkException.CreateBindException(ex.NativeErrno);
        }
    }

    public void Unbind(string address)
    {
        try
        {
            Kernel.Unbind(address);
        }
        catch (ZlinkException ex)
        {
            throw ZlinkException.CreateConnectException(ex.NativeErrno);
        }
    }

    public ISocketMonitor MonitorOpen(SocketEvent events = SocketEvent.All)
    {
        EnumValidation.EnsureSocketEvents(events, nameof(events));
        try
        {
            return Kernel.MonitorOpen(events);
        }
        catch (ZlinkException ex)
        {
            throw ZlinkException.CreateConfigException(ex.NativeErrno);
        }
    }

    public void SetChannelName(string channelName)
    {
        SetChannelNameCore(channelName);
    }

    public void SetTlsServer(string certPath, string keyPath,
        bool requireClientCert = false)
    {
        if (certPath == null)
            throw new ArgumentNullException(nameof(certPath));
        if (keyPath == null)
            throw new ArgumentNullException(nameof(keyPath));

        var rc = NativeMethods.zlink_set_tls_server(Handle, certPath, keyPath,
            requireClientCert ? 1 : 0);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    public void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false)
    {
        if (caCertPath == null)
            throw new ArgumentNullException(nameof(caCertPath));
        if (hostname == null)
            throw new ArgumentNullException(nameof(hostname));

        var rc = NativeMethods.zlink_set_tls_client(Handle, caCertPath, hostname,
            trustSystem ? 1 : 0);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    public void Close()
    {
        Kernel.Close();
    }

    public void Dispose()
    {
        try
        {
            Kernel.Dispose();
        }
        catch (ZlinkException ex)
        {
            throw ZlinkException.CreateCloseException(ex.NativeErrno);
        }

        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    void ISocketOptionEndpoint.SetOption(SocketOptionKey<int> option, int value)
    {
        SetOption(option, value);
    }

    void ISocketOptionEndpoint.SetOption(SocketOptionKey<long> option,
        long value)
    {
        SetOption(option, value);
    }

    void ISocketOptionEndpoint.SetOption(SocketOptionKey<ulong> option,
        ulong value)
    {
        SetOption(option, value);
    }

    void ISocketOptionEndpoint.SetOption(SocketOptionKey<byte[]> option,
        byte[] value)
    {
        SetOption(option, value);
    }

    void ISocketOptionEndpoint.SetOption(SocketOptionKey<byte[]> option,
        ReadOnlySpan<byte> value)
    {
        SetOption(option, value);
    }

    void ISocketOptionEndpoint.SetOption(SocketOptionKey<string> option,
        string value)
    {
        SetOption(option, value);
    }

    int ISocketOptionEndpoint.GetOption(SocketOptionKey<int> option)
    {
        return GetOption(option);
    }

    long ISocketOptionEndpoint.GetOption(SocketOptionKey<long> option)
    {
        return GetOption(option);
    }

    ulong ISocketOptionEndpoint.GetOption(SocketOptionKey<ulong> option)
    {
        return GetOption(option);
    }

    byte[] ISocketOptionEndpoint.GetOption(SocketOptionKey<byte[]> option,
        int initialSize)
    {
        return GetOption(option, initialSize);
    }

    int ISocketOptionEndpoint.GetOption(SocketOptionKey<byte[]> option,
        Span<byte> destination)
    {
        return GetOption(option, destination);
    }

    string ISocketOptionEndpoint.GetOption(SocketOptionKey<string> option,
        int initialSize)
    {
        return GetOption(option, initialSize);
    }

    SocketType ISocketOptionEndpoint.SocketType => Kernel.Type;

    internal void SetChannelNameCore(string channelName)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        var rc = NativeMethods.zlink_socket_set_channel_name(Handle, channelName);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    internal string GetChannelNameCore()
    {
        var buffer = new byte[256];
        var rc = NativeMethods.zlink_socket_get_channel_name(Handle, buffer,
            (nuint)buffer.Length, out var length);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        return Encoding.UTF8.GetString(buffer, 0, checked((int)length));
    }

    internal string GetChannelName()
    {
        return GetChannelNameCore();
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal void SetOption(SocketOptionKey<int> option, int value)
    {
        Kernel.SetOption(option, value);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal void SetOption(SocketOptionKey<long> option, long value)
    {
        Kernel.SetOption(option, value);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal void SetOption(SocketOptionKey<ulong> option, ulong value)
    {
        Kernel.SetOption(option, value);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal void SetOption(SocketOptionKey<byte[]> option, byte[] value)
    {
        Kernel.SetOption(option, value);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal void SetOption(SocketOptionKey<byte[]> option, ReadOnlySpan<byte> value)
    {
        Kernel.SetOption(option, value);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal void SetOption(SocketOptionKey<string> option, string value)
    {
        Kernel.SetOption(option, value);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal int GetOption(SocketOptionKey<int> option)
    {
        return Kernel.GetOption(option);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal long GetOption(SocketOptionKey<long> option)
    {
        return Kernel.GetOption(option);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal ulong GetOption(SocketOptionKey<ulong> option)
    {
        return Kernel.GetOption(option);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal byte[] GetOption(SocketOptionKey<byte[]> option, int initialSize = 256)
    {
        return Kernel.GetOption(option, initialSize);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal int GetOption(SocketOptionKey<byte[]> option, Span<byte> destination)
    {
        return Kernel.GetOption(option, destination);
    }

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal string GetOption(SocketOptionKey<string> option, int initialSize = 256)
    {
        return Kernel.GetOption(option, initialSize);
    }
}