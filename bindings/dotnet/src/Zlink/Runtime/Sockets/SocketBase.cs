// SPDX-License-Identifier: MPL-2.0

using System;
using System.ComponentModel;
using System.Threading.Tasks;
using Systems.Zlink.Runtime.Sockets.Internal;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

[EditorBrowsable(EditorBrowsableState.Never)]
internal abstract class SocketBase : ISocket, ISocketOptionEndpoint
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
    internal object SubmitGate { get; } = new();
    public CommonSocketOptions Options { get; }

    public void Bind(string address)
    {
        try
        {
            _kernel.Bind(address);
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
            _kernel.Unbind(address);
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
            return _kernel.MonitorOpen(events);
        }
        catch (ZlinkException ex)
        {
            throw ZlinkException.CreateConfigException(ex.NativeErrno);
        }
    }

    internal void SetChannelNameCore(string channelName)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        int rc = NativeMethods.zlink_socket_set_channel_name(Handle, channelName);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    internal string GetChannelNameCore()
    {
        byte[] buffer = new byte[256];
        int rc = NativeMethods.zlink_socket_get_channel_name(Handle, buffer,
            (nuint)buffer.Length, out nuint length);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        return System.Text.Encoding.UTF8.GetString(buffer, 0, checked((int)length));
    }

    public void SetChannelName(string channelName)
    {
        SetChannelNameCore(channelName);
    }

    internal string GetChannelName()
    {
        return GetChannelNameCore();
    }

    public void SetTlsServer(string certPath, string keyPath,
        bool requireClientCert = false)
    {
        if (certPath == null)
            throw new ArgumentNullException(nameof(certPath));
        if (keyPath == null)
            throw new ArgumentNullException(nameof(keyPath));

        int rc = NativeMethods.zlink_set_tls_server(Handle, certPath, keyPath,
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

        int rc = NativeMethods.zlink_set_tls_client(Handle, caCertPath, hostname,
            trustSystem ? 1 : 0);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    public void Close()
    {
        _kernel.Close();
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

    void ISocketOptionEndpoint.SetOption(SocketOptionKey<int> option, int value)
        => SetOption(option, value);

    void ISocketOptionEndpoint.SetOption(SocketOptionKey<long> option,
        long value) => SetOption(option, value);

    void ISocketOptionEndpoint.SetOption(SocketOptionKey<ulong> option,
        ulong value) => SetOption(option, value);

    void ISocketOptionEndpoint.SetOption(SocketOptionKey<byte[]> option,
        byte[] value) => SetOption(option, value);

    void ISocketOptionEndpoint.SetOption(SocketOptionKey<byte[]> option,
        ReadOnlySpan<byte> value) => SetOption(option, value);

    void ISocketOptionEndpoint.SetOption(SocketOptionKey<string> option,
        string value) => SetOption(option, value);

    int ISocketOptionEndpoint.GetOption(SocketOptionKey<int> option)
        => GetOption(option);

    long ISocketOptionEndpoint.GetOption(SocketOptionKey<long> option)
        => GetOption(option);

    ulong ISocketOptionEndpoint.GetOption(SocketOptionKey<ulong> option)
        => GetOption(option);

    byte[] ISocketOptionEndpoint.GetOption(SocketOptionKey<byte[]> option,
        int initialSize) => GetOption(option, initialSize);

    int ISocketOptionEndpoint.GetOption(SocketOptionKey<byte[]> option,
        Span<byte> destination) => GetOption(option, destination);

    string ISocketOptionEndpoint.GetOption(SocketOptionKey<string> option,
        int initialSize) => GetOption(option, initialSize);

    SocketType ISocketOptionEndpoint.SocketType => _kernel.Type;

    public void Dispose()
    {
        try
        {
            _kernel.Dispose();
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
}
